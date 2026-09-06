// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "archivepath.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QPaintEvent>
#include <QOpenGLWidget>
#include <QScrollBar>
#include <QRubberBand>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QPointer>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QtMath>
#include <atomic>
#include <cmath>
#include <algorithm>


ImageView::ImageView(QWidget *parent)
    : QGraphicsView(parent)
    , m_gallery(this)
    , m_workspace(this)
    , m_image(this)
{
    m_scene = new QGraphicsScene(this);
    // BSP indexing is fragile with frequent add/remove (Duplicate + Delete):
    // deferred paints can walk a tree that still holds freed items. Linear
    // search is fine for Workspace/Gallery counts we care about.
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        emit statusChanged();
        emit canvasSelectionChanged();
    });
    m_undoStack = new QUndoStack(this);
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<quint64>("quint64");

    // Durable native size from thumtoo (GUI thread via Qt Executor).
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::sizeReady, this,
            [this](const QString &path, const QSize &size) {
                if (path.isEmpty() || !size.isValid() || size.width() <= 0
                    || size.height() <= 0) {
                    return;
                }
                m_sizeProbeScheduled.remove(path);
                // Prefer a size already learned from a full decode.
                if (m_imageSizeByPath.contains(path) && !isProvisionalImageSize(path)) {
                    return;
                }
                rememberImageSize(path, size);
                applyProbedImageSize(path, size);
            });

    // Soft preview upgrade when thumtoo finishes a ladder level for a path.
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            [this](const QString &path, int maxEdge) {
                if (path.isEmpty()) {
                    return;
                }
                const int edge = maxEdge > 0 ? maxEdge : ImageCache::kPreviewEdge;
                const QPointer<ImageView> guard(this);
                QThreadPool::globalInstance()->start([guard, path, edge]() {
                    const QImage preview = ImageLoader::loadThumbnail(path, edge);
                    if (!guard || preview.isNull()) {
                        return;
                    }
                    QMetaObject::invokeMethod(guard, [guard, path, preview]() {
                        if (!guard) {
                            return;
                        }
                        // LoadAdd: refresh gallery/workspace/image tiles that still
                        // show placeholders; do not fight a completed full decode.
                        guard->onImagePreviewLoaded(
                            path, preview, 0,
                            static_cast<int>(ImageView::LoadAdd));
                    });
                });
            });

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hudVisible || m_hudFlashVisible || m_slideshowPausedHud) {
            viewport()->update();
        }
    });

    m_hudFlashTimer = new QTimer(this);
    m_hudFlashTimer->setSingleShot(true);
    m_layoutDebounceTimer = new QTimer(this);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(0);
    connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
        // Automatic debounce pack removed (Phase 1). Explicit applyLayout only.
        Q_UNUSED(this);
    });
    connect(m_hudFlashTimer, &QTimer::timeout, this, [this]() {
        m_hudFlashVisible = false;
        m_hudIdentityPulse = false;
        m_hudAction.clear();
        m_hudDetail.clear();
        viewport()->update();
    });

    m_slideshowProgressTimer = new QTimer(this);
    m_slideshowProgressTimer->setInterval(33); // ~30 Hz; cheap 1px redraw
    connect(m_slideshowProgressTimer, &QTimer::timeout, this, [this]() {
        if (m_hudVisible && m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0) {
            viewport()->update();
        }
    });

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    // OpenGL viewport — overlays must use drawForeground (see paintEvent).
    setViewport(new QOpenGLWidget);
    setAcceptDrops(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(m_bgColor));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    // QGraphicsView delivers moves via the viewport — both need tracking or
    // MouseMove only arrives while a button is held (breaks slideshow cursor).
    setMouseTracking(true);
    if (viewport()) {
        viewport()->setMouseTracking(true);
    }
    setAlignment(Qt::AlignCenter);
    // Full updates: HUD, edge affordances and workspace chrome are painted in
    // paintEvent on top of the scene. SmartViewportUpdate scrolls/blits the
    // viewport and leaves overlay trails (e.g. Vertical gallery scroll).
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    // Scrolling moves tiles under a stationary cursor — refresh gallery HUD path.
    auto refreshHover = [this]() {
        if (isGalleryMode() && !m_lastHoverViewPos.isNull()) {
            updateGalleryHoverAt(m_lastHoverViewPos);
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
    });
}

ImageView::~ImageView()
{
    // Invalidate any queued onImageLoaded invocations from the thread pool.
    ++m_loadGeneration;

    if (m_hudFlashTimer) {
        m_hudFlashTimer->stop();
    }
    if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
    }
    if (m_layoutDebounceTimer) {
        m_layoutDebounceTimer->stop();
    }

    // Scene clear emits selectionChanged; our handler calls viewport()->update().
    // That is unsafe once ~QWidget has started deleting children — tear the
    // scene down here while ImageView is still fully constructed.
    discardStashedGallery();

    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
        m_scene->blockSignals(true);
        m_scene->clear();
        m_items.clear();
        m_pendingWorkspacePaths.clear();
        m_galleryDecodeScheduled.clear();
        m_galleryDecodeFailed.clear();
        setScene(nullptr);
        delete m_scene;
        m_scene = nullptr;
    }
}



QSize ImageView::probeImageSize(const QString &path) const
{
    // Archive member probes must not extract on the GUI thread (large zip
    // open would freeze Gallery virtualization). Prefer thumtoo cache-only
    // size; otherwise a neutral placeholder until ladder/sizeReady reflows.
    if (ArchivePath::isArchiveRef(path)) {
        if (const QSize cached = ThumtooCache::cachedSize(path); cached.isValid()) {
            return cached;
        }
        return QSize(1024, 1024);
    }
    // Header-only when possible (Qt, then VIPS); see ImageLoader::probeSize.
    QSize s = ImageLoader::probeSize(path);
    if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
        s = QSize(1000, 1000);
    }
    return s;
}

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || !size.isValid() || size.width() <= 0 || size.height() <= 0) {
        return;
    }
    m_imageSizeByPath.insert(path, size);
    m_provisionalSizePaths.remove(path);
}

bool ImageView::isProvisionalImageSize(const QString &path) const
{
    return !path.isEmpty() && m_provisionalSizePaths.contains(path);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return QSize(1000, 1000);
    }
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend()) {
        return it.value();
    }
    // Cache-only durable size (files and //archive: members) — no source I/O.
    if (const QSize cached = ThumtooCache::cachedSize(path); cached.isValid()) {
        rememberImageSize(path, cached);
        return cached;
    }
    // Archives: schedule thumtoo probe; neutral stand-in until sizeReady.
    if (ArchivePath::isArchiveRef(path)) {
        scheduleImageSizeProbe(path);
        m_provisionalSizePaths.insert(path);
        return QSize(1024, 1024);
    }
    // Unknown size: do not touch the filesystem on the GUI thread. Neutral
    // geometry until async probe, preview aspect, or full decode fills the cache.
    // Callers that have a preview should use layoutSizeForPath(path, preview).
    scheduleImageSizeProbe(path);
    m_provisionalSizePaths.insert(path);
    return QSize(1000, 1000);
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    const auto it = m_imageSizeByPath.constFind(path);
    if (it != m_imageSizeByPath.cend()) {
        return it.value();
    }
    // Aspect from preview so fitInView frames the window correctly; magnitude
    // only needs to be stable (view scale absorbs absolute size).
    if (!previewHint.isNull() && previewHint.width() > 0 && previewHint.height() > 0) {
        scheduleImageSizeProbe(path);
        m_provisionalSizePaths.insert(path);
        return previewHint.size();
    }
    return imageSizeForPath(path);
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_imageSizeByPath.contains(path) || m_sizeProbeScheduled.contains(path)) {
        return;
    }
    // Archive members: only durable-cache probe (sizeReady applies the result).
    // Never extract the container on the GUI thread or a local pool worker.
    if (ArchivePath::isArchiveRef(path)) {
        m_sizeProbeScheduled.insert(path);
        ThumtooCache::scheduleProbe(path);
        return;
    }
    m_sizeProbeScheduled.insert(path);
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path]() {
        QSize s = ImageLoader::probeSize(path);
        if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
            s = QSize(1000, 1000);
        }
        if (!guard) {
            return;
        }
        ImageView *view = guard.data();
        if (!view) {
            return;
        }
        QMetaObject::invokeMethod(view, [guard, path, s]() {
            ImageView *const host = guard.data();
            if (!host) {
                return;
            }
            host->m_sizeProbeScheduled.remove(path);
            // Prefer a size already learned from a full decode.
            if (host->m_imageSizeByPath.contains(path)) {
                return;
            }
            host->rememberImageSize(path, s);
            host->applyProbedImageSize(path, s);
        }, Qt::QueuedConnection);
    });
}

void ImageView::applyProbedImageSize(const QString &path, const QSize &size)
{
    if (path.isEmpty() || !size.isValid()) {
        return;
    }
    bool any = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        if (item->hasDecodedPixels()) {
            continue;
        }
        const QSize cur = item->imageSize();
        if (cur == size) {
            continue;
        }
        item->setIntrinsicSize(size);
        any = true;
        if (isImageMode() && item == targetItem()) {
            if (m_slideshowProgressActive
                && m_slideshowMotion == SlideshowMotion::Off) {
                applySlideshowZoomFraming(item);
            } else if (!m_slideshowProgressActive) {
                fitItem(item, currentFitAspectMode());
            }
            if (m_scene) {
                m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
            }
        }
    }
    if (any && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (any && viewport()) {
        viewport()->update();
    }
}

int ImageView::pendingDecodeCount() const
{
    // m_galleryDecodeScheduled ⊆ m_pendingWorkspacePaths for gallery window loads;
    // do not double-count.
    {
        int pendingAdds = 0;
        for (int n : m_pendingWorkspacePaths) {
            pendingAdds += n;
        }
        return pendingAdds + m_pendingRestoreStates.size();
    }
}















































Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_fillMode ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
}



QSize ImageView::imageSize() const
{
    if (ImageItem *item = targetItem()) {
        return item->imageSize();
    }
    if (ImageItem *item = primaryItem()) {
        return item->imageSize();
    }
    return {};
}

int ImageView::itemCount() const
{
    return m_items.size();
}

QStringList ImageView::itemPaths() const
{
    QStringList paths;
    for (ImageItem *item : m_items) {
        paths.append(item->path());
    }
    return paths;
}

QStringList ImageView::selectedPaths() const
{
    QStringList paths;
    if (isImageMode()) {
        if (ImageItem *item = primaryItem()) {
            paths.append(item->path());
        } else if (hasClassicPath()) {
            paths.append(classicPath());
        }
        return paths;
    }
    // Gallery / Workspace: preserve session/canvas order, not click order.
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            paths.append(item->path());
        }
    }
    return paths;
}





void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Rapid edge clicks arrive as double-clicks (second press is not a Press event).
    // Treat them as navigation, same as a single click on the affordance.
    if (isImageMode() && event->button() == Qt::LeftButton
        && !(event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        const EdgeZone zone = edgeZoneAt(event->pos());
        if (zone == EdgeZone::Previous) {
            emit navigatePreviousRequested();
            event->accept();
            return;
        }
        if (zone == EdgeZone::Next) {
            emit navigateNextRequested();
            event->accept();
            return;
        }
        emit fullscreenToggleRequested();
        event->accept();
        return;
    }

    // Gallery: double-click opens the tile in Image mode (classic file view).
    // Prefer SessionImageId so duplicate paths open the correct session row.
    if (isGalleryMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (item->sessionId() != kInvalidSessionImageId) {
                    emit sessionImageOpenRequested(item->sessionId());
                } else if (item->sessionIndex() >= 0) {
                    emit sessionSlotOpenRequested(item->sessionIndex());
                } else {
                    const QString path = item->path();
                    if (!path.isEmpty()) {
                        emit galleryItemOpenRequested(path);
                    }
                }
                event->accept();
                return;
            }
        }
        event->accept();
        return;
    }

    // Workspace: double-click on chrome starts a handle drag; on the image
    // body opens Image mode (same path as Gallery). Empty space is swallowed
    // so the missing second press does not clear selection via the base class.
    if (isWorkspaceMode() && event->button() == Qt::LeftButton
        && m_tool == Tool::Select && m_scene) {
        const QPointF scenePos = mapToScene(event->pos());
        // Selected item handles first (chrome is above tiles).
        QList<ImageItem *> selected;
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    selected.append(ii);
                }
            }
        }
        if (selected.size() == 1) {
            ImageItem *item = selected.first();
            if (item->beginHandleInteraction(scenePos, event->modifiers())) {
                m_handleDragItem = item;
                m_dragItem = item;
                m_dragStartState = captureState(item);
                event->accept();
                return;
            }
        } else if (selected.size() > 1) {
            const int gh = groupHandleAt(event->pos(), selected);
            if (gh >= 0 && beginGroupScale(gh, selected)) {
                event->accept();
                return;
            }
        }
        // Image body under cursor → Image mode for *this* session slot
        // (path-only open would always hit the first duplicate in the session).
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    if (ii->sessionId() != kInvalidSessionImageId) {
                        emit sessionImageOpenRequested(ii->sessionId());
                    } else if (ii->sessionIndex() >= 0) {
                        emit sessionSlotOpenRequested(ii->sessionIndex());
                    } else {
                        const QString path = ii->path();
                        if (!path.isEmpty()) {
                            emit galleryItemOpenRequested(path);
                        }
                    }
                    event->accept();
                    return;
                }
            }
        }
        event->accept();
        return;
    }

    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    if (m_mouseInfo.valid) {
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
    }
    if (m_hoverEdge != EdgeZone::None) {
        m_hoverEdge = EdgeZone::None;
        viewport()->update();
    }
    if (!m_gallery.hoverPath().isEmpty()) {
        m_gallery.clearHoverPath();
        viewport()->update();
    }
    QGraphicsView::leaveEvent(event);
}

