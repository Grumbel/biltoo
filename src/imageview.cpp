// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QPointer>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPaintEvent>
#include <QScrollBar>
#include <QRubberBand>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
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
{
    m_scene = new QGraphicsScene(this);
    // BSP indexing is fragile with frequent add/remove (Duplicate + Delete):
    // deferred paints can walk a tree that still holds freed items. Linear
    // search is fine for Workspace/Gallery counts we care about.
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        emit statusChanged();
    });
    m_undoStack = new QUndoStack(this);
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<quint64>("quint64");

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hudVisible || m_hudFlashVisible) {
            viewport()->update();
        }
    });

    m_hudFlashTimer = new QTimer(this);
    m_hudFlashTimer->setSingleShot(true);
    m_layoutDebounceTimer = new QTimer(this);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(0);
    connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
        if (isGalleryMode() && !m_items.isEmpty()) {
            applyLayout();
        }
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
    setAcceptDrops(true);
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(QColor(36, 36, 36)));
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
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

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image)
{
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    auto *item = new ImageItem(path, intrinsicSize);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

QSize ImageView::probeImageSize(const QString &path) const
{
    // Header-only when possible (Qt, then VIPS); see ImageLoader::probeSize.
    QSize s = ImageLoader::probeSize(path);
    if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
        // Last resort so pack has geometry until the first full decode reflows.
        s = QSize(1000, 1000);
    }
    return s;
}

int ImageView::pendingDecodeCount() const
{
    // m_galleryDecodeScheduled ⊆ m_pendingWorkspacePaths for gallery window loads;
    // do not double-count.
    return m_pendingWorkspacePaths.size() + m_pendingRestoreStates.size();
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == LoadAdd) {
        m_pendingWorkspacePaths.insert(path);
    }
    // LoadRestore pending is owned by m_pendingRestoreStates (AUDIT M27).
    // AUDIT H3a: only LoadReplace advances the generation token so workspace
    // adds cannot cancel an in-flight Image-mode navigation decode.
    // (Cannot use ?: on atomic — pre-increment yields T, bare atomic is not T.)
    quint64 gen = m_loadGeneration.load();
    if (role == LoadReplace) {
        gen = ++m_loadGeneration;
    }
    emit statusChanged(); // pending count for status bar
    // QPointer: worker must not invokeMethod on a destroyed view (secondary
    // windows with WA_DeleteOnClose, app exit). Generation only filters
    // superseding once the slot runs on the GUI thread.
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(QImage, image),
                                  Q_ARG(quint64, gen),
                                  Q_ARG(int, static_cast<int>(role)));
    });
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    if (path.isEmpty() || m_galleryDecodeFailed.contains(path)
        || m_galleryDecodeScheduled.contains(path)
        || m_pendingWorkspacePaths.contains(path)) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (item && item->hasDecodedPixels()) {
        return;
    }
    if (m_galleryDecodeScheduled.size() >= kMaxConcurrentGalleryDecodes) {
        return; // caller (updateGalleryDecodeWindow) will retry after a slot frees
    }
    m_galleryDecodeScheduled.insert(path);
    m_pendingWorkspacePaths.insert(path);
    emit statusChanged();
    const quint64 gen = m_loadGeneration.load();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                  Q_ARG(QString, path),
                                  Q_ARG(QImage, image),
                                  Q_ARG(quint64, gen),
                                  Q_ARG(int, static_cast<int>(LoadAdd)));
    });
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Replace loads only care about the latest request
    if (role == LoadReplace) {
        if (generation != m_loadGeneration) {
            return; // superseded by a newer navigation / open
        }
        if (path != m_classicPath || isMultiItemMode()) {
            // Stale image-mode navigation or switched to workspace
            if (!(isMultiItemMode() && m_items.isEmpty() && path == m_classicPath)) {
                if (path != m_classicPath) {
                    return;
                }
            }
        }
        if (image.isNull()) {
            m_lastLoadError = path;
            emit statusChanged();
            return;
        }
        if (isImageMode()) {
            m_lastLoadError.clear();
            // Suppress paints between removing the old item and fitting the new one
            // so we never present a native-scale (or empty) intermediate frame.
            setUpdatesEnabled(false);
            clearWorkspace();
            ImageItem *item = createItemFromImage(path, image);
            if (!item) {
                setUpdatesEnabled(true);
                m_lastLoadError = path;
                emit statusChanged();
                return;
            }
            // Never inherit Gallery/Workspace placement or scale.
            // DOMAIN: user rotation/flips persist across navigation (AUDIT H6).
            item->setInteractive(false);
            item->setScaleHandlesEnabled(false);
            item->setItemScale(1.0);
            item->setPos(0, 0);
            {
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    item->setItemRotation(it->rotation);
                    item->setItemHFlip(it->hFlip);
                    item->setItemVFlip(it->vFlip);
                } else {
                    item->setItemRotation(0.0);
                }
            }
            prepareImageModeCanvas();
            fitItem(item, currentFitAspectMode());
            m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
            setUpdatesEnabled(true);
            viewport()->update();
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image
        if (m_items.isEmpty()) {
            ImageItem *item = createItemFromImage(path, image);
            if (item) {
                item->setSelected(true);
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
                emit statusChanged();
            }
        }
        return;
    }

    // Workspace add / restore
    if (role == LoadRestore) {
        // AUDIT M27: claim one pending restore state for this path (duplicates OK).
        int claim = -1;
        for (int i = 0; i < m_pendingRestoreStates.size(); ++i) {
            if (m_pendingRestoreStates.at(i).path == path) {
                claim = i;
                break;
            }
        }
        if (claim < 0) {
            return;
        }
        const WorkspaceItemState state = m_pendingRestoreStates.takeAt(claim);
        if (image.isNull()) {
            return;
        }
        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            return;
        }
        applyState(item, state);
        if (m_layoutMode != LayoutMode::FreeForm) {
            applyLayout();
        }
        emit statusChanged();
        emit workspacePathsChanged();
        return;
    }

    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window
    m_galleryDecodeScheduled.remove(path);
    if (!m_pendingWorkspacePaths.contains(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        emit statusChanged();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        return;
    }
    m_pendingWorkspacePaths.remove(path);

    ImageItem *existing = findItemByPath(path);
    if (!existing) {
        // Decode may finish while Gallery tiles are stashed (user in Image mode).
        for (ImageItem *cand : m_stashedGalleryItems) {
            if (cand && cand->path() == path) {
                existing = cand;
                break;
            }
        }
    }
    if (existing) {
        if (image.isNull()) {
            m_galleryDecodeFailed.insert(path);
            emit statusChanged();
            if (isGalleryMode()) {
                updateGalleryDecodeWindow();
            }
            return;
        }
        const QSize before = existing->imageSize();
        existing->setSourceImage(image);
        if (isGalleryMode()) {
            // Probe size can differ from decoded size — reflow so scale/pos stay valid.
            if (before != image.size()) {
                scheduleApplyLayout();
            } else {
                existing->update();
            }
        } else if (m_layoutMode != LayoutMode::FreeForm) {
            applyLayout();
        }
        emit statusChanged();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        return;
    }

    if (image.isNull()) {
        m_galleryDecodeFailed.insert(path);
        emit statusChanged();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        return;
    }

    // Do not spawn Gallery tiles onto the Image-mode canvas.
    if (isImageMode()) {
        emit statusChanged();
        return;
    }

    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        emit statusChanged();
        return;
    }
    // Flags already match ViewMode via createItemFromImage / applyItemModeFlags.
    // Do not force interactive — that flashes handles in Gallery.

    // Drop position, remembered state, or empty-space placement
    if (m_pendingScenePos.contains(path)) {
        const QPointF pos = m_pendingScenePos.take(path);
        item->setPos(pos);
        item->setItemScale(1.0);
        item->setItemRotation(0.0);
        item->setItemOpacity(1.0);
        item->setStackZ(m_items.size() - 1);
    } else {
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.cend()) {
            applyState(item, *it);
        } else {
            WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
            const QSizeF sz(image.width(), image.height());
            s.pos = findEmptyPlacement(sz);
            applyState(item, s);
        }
    }

    if (m_layoutMode != LayoutMode::FreeForm) {
        if (!m_pathOrder.isEmpty()) {
            reorderItemsByPaths(m_pathOrder);
        }
        applyLayout();
    } else {
        updateWorkspaceSceneRect();
    }
    emit statusChanged();
    emit workspacePathsChanged();
    if (isGalleryMode()) {
        updateGalleryDecodeWindow();
    }
}

void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    if (m_tool == Tool::Pan) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::ArrowCursor);
    }
    // Workspace Select: rubber-band multi-select on empty drag (same as Gallery).
    // Pan tool keeps NoDrag so middle/Alt pan stays the only view pan path.
    if (isWorkspaceMode()) {
        setDragMode(m_tool == Tool::Select ? QGraphicsView::RubberBandDrag
                                           : QGraphicsView::NoDrag);
    }
    emit toolChanged(m_tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (m_imageModeNavEnabled == on) {
        return;
    }
    m_imageModeNavEnabled = on;
    if (!on && m_hoverEdge != EdgeZone::GalleryReturn) {
        m_hoverEdge = EdgeZone::None;
    }
    viewport()->update();
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    if (m_galleryReturnAvailable == on) {
        return;
    }
    m_galleryReturnAvailable = on;
    if (!on && m_hoverEdge == EdgeZone::GalleryReturn) {
        m_hoverEdge = EdgeZone::None;
    }
    viewport()->update();
}

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || color == m_bgColor) {
        return;
    }
    m_bgColor = color;
    viewport()->update();
}

void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!color.isValid() || color == m_bgColorAlt) {
        return;
    }
    m_bgColorAlt = color;
    viewport()->update();
}

void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (m_bgPattern == pattern) {
        return;
    }
    m_bgPattern = pattern;
    viewport()->update();
}

void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (m_bgCheckerWorkspaceOnly == on) {
        return;
    }
    m_bgCheckerWorkspaceOnly = on;
    viewport()->update();
}

bool ImageView::loadImage(const QString &path)
{
    m_classicPath = path;
    m_lastLoadError.clear();

    if (isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_items.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return true;
    }

    // Classic mode: decode off the GUI thread. Keep the previous image and its
    // fit transform until the new decode arrives — resetting the transform here
    // would show the old pixmap at 1:1 for a frame (glitchy rapid navigation).
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}

qreal ImageView::viewScale() const
{
    const QTransform t = transform();
    return std::hypot(t.m11(), t.m12());
}

void ImageView::refreshStatus()
{
    emit statusChanged();
    if (m_hudVisible || m_hudFlashVisible) {
        viewport()->update();
    }
}

void ImageView::zoomViewBy(qreal factor)
{
    // Gallery: view zoom is allowed for inspection (matches wheel zoom). Pack /
    // resize still resets the view transform so tiles stay layout-correct
    // (AUDIT M4 — one policy: zoom works until next pack).
    m_fitMode = false;
    m_fillMode = false;
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Handle sizes depend on view scale — refresh geometry for selected items
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->updateHandleLayout();
        }
    }
    emit statusChanged();
}

void ImageView::zoomIn()
{
    // View-level zoom in Image mode and free-form Workspace
    zoomViewBy(1.25);
}

void ImageView::zoomOut()
{
    zoomViewBy(1.0 / 1.25);
}

void ImageView::zoomReset()
{
    m_fitMode = false;
    m_fillMode = false;
    if (isMultiItemMode()) {
        resetTransform();
        emit statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        resetTransform();
        centerOn(item);
        emit statusChanged();
    }
}

void ImageView::zoomFit()
{
    m_fitMode = true;
    m_fillMode = false;
    if (isGalleryMode()) {
        // Fit the packed gallery into the viewport. applyLayout() alone only
        // resets to identity after a view-zoom when the pack already matches
        // the window — use fitInView so +/- zoom is actually undone to "all
        // tiles visible".
        if (!m_items.isEmpty()) {
            const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatio);
            }
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatio);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatio);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        emit statusChanged();
    }
}

void ImageView::zoomFill()
{
    m_fitMode = true;
    m_fillMode = true;
    if (isGalleryMode()) {
        if (!m_items.isEmpty()) {
            const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatioByExpanding);
            }
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(m_scene->itemsBoundingRect().adjusted(-32, -32, 32, 32),
                      Qt::KeepAspectRatioByExpanding);
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemScale(1.0);
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatioByExpanding);
        emit statusChanged();
    }
}

void ImageView::armZoomRegion()
{
    if (m_items.isEmpty() && isImageMode() && m_classicPath.isEmpty()) {
        return;
    }
    cancelZoomRegion();
    m_zoomRegionArmed = true;
    setCursor(Qt::CrossCursor);
    emit statusChanged();
    viewport()->update();
}

void ImageView::cancelZoomRegion()
{
    m_zoomRegionArmed = false;
    m_zoomRegionDragging = false;
    if (m_zoomRubberBand) {
        m_zoomRubberBand->hide();
    }
    if (!m_panning && !m_rotating) {
        setCursor(m_tool == Tool::Pan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    emit statusChanged();
}


void ImageView::flipHorizontal()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        item->toggleHFlip();
        rememberItemState(item);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout();
    }
    emit statusChanged();
}

void ImageView::flipVertical()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        item->toggleVFlip();
        rememberItemState(item);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout();
    }
    emit statusChanged();
}

ImageItem *ImageView::cropTargetItem() const
{
    if (isGalleryMode()) {
        return nullptr;
    }
    if (ImageItem *t = targetItem()) {
        if (t->hasDecodedPixels() || !t->pixmap().isNull()) {
            return t;
        }
    }
    return primaryItem();
}

void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        m_cropRect = QRectF();
        return;
    }
    const QRectF cr = item->contentRect();
    if (!m_cropRect.isValid() || m_cropRect.isEmpty()) {
        m_cropRect = cr;
        return;
    }
    m_cropRect = m_cropRect.normalized().intersected(cr);
    if (m_cropRect.width() < 1.0) {
        m_cropRect.setWidth(1.0);
    }
    if (m_cropRect.height() < 1.0) {
        m_cropRect.setHeight(1.0);
    }
    // Keep inside content after min-size clamp.
    if (m_cropRect.right() > cr.right()) {
        m_cropRect.moveRight(cr.right());
    }
    if (m_cropRect.bottom() > cr.bottom()) {
        m_cropRect.moveBottom(cr.bottom());
    }
    if (m_cropRect.left() < cr.left()) {
        m_cropRect.moveLeft(cr.left());
    }
    if (m_cropRect.top() < cr.top()) {
        m_cropRect.moveTop(cr.top());
    }
}

void ImageView::setCropMode(bool on)
{
    if (on == m_cropMode) {
        return;
    }
    if (on) {
        if (isGalleryMode()) {
            return;
        }
        ImageItem *item = cropTargetItem();
        if (!item || (!item->hasDecodedPixels() && item->pixmap().isNull())) {
            flashHud(tr("Crop"), tr("No image"));
            return;
        }
        cancelZoomRegion();
        m_cropMode = true;
        m_cropRect = item->contentRect();
        m_cropActiveHandle = CropHandle::None;
        m_cropHoverHandle = CropHandle::None;
        m_cropRubberBanding = false;
        flashHud(tr("Crop mode"), tr("Drag handles · Enter apply · Esc cancel"));
        emit cropModeChanged(true);
        emit statusChanged();
        viewport()->update();
        return;
    }
    leaveCropModeInternal(false);
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_cropMode);
}

void ImageView::applyCrop()
{
    leaveCropModeInternal(true);
}

void ImageView::cancelCrop()
{
    leaveCropModeInternal(false);
}

void ImageView::leaveCropModeInternal(bool apply)
{
    if (!m_cropMode) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (apply && item) {
        ensureCropRectValid();
        const QRectF full = item->contentRect();
        // Skip no-op full-frame crops.
        if (m_cropRect.isValid()
            && (qAbs(m_cropRect.left() - full.left()) > 0.5
                || qAbs(m_cropRect.top() - full.top()) > 0.5
                || qAbs(m_cropRect.width() - full.width()) > 0.5
                || qAbs(m_cropRect.height() - full.height()) > 0.5)) {
            if (item->cropToLocalRect(m_cropRect)) {
                rememberItemState(item);
                if (isImageMode()) {
                    m_fitMode = true;
                    fitItem(item, currentFitAspectMode());
                } else if (isWorkspaceMode()) {
                    updateWorkspaceSceneRect();
                }
                flashHud(tr("Cropped"),
                         QStringLiteral("%1×%2")
                             .arg(item->imageSize().width())
                             .arg(item->imageSize().height()));
            }
        }
    }
    m_cropMode = false;
    m_cropRect = QRectF();
    m_cropActiveHandle = CropHandle::None;
    m_cropHoverHandle = CropHandle::None;
    m_cropRubberBanding = false;
    emit cropModeChanged(false);
    emit statusChanged();
    viewport()->unsetCursor();
    viewport()->update();
}

QRectF ImageView::cropRectView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return QRectF();
    }
    const QPolygonF poly = item->mapToScene(m_cropRect);
    QRectF sceneBounds = poly.boundingRect();
    const QPoint tl = mapFromScene(sceneBounds.topLeft());
    const QPoint br = mapFromScene(sceneBounds.bottomRight());
    return QRectF(tl, br).normalized();
}

ImageView::CropHandle ImageView::cropHandleAt(const QPoint &viewPos) const
{
    if (!m_cropMode) {
        return CropHandle::None;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return CropHandle::None;
    }
    // Map crop rect corners through item → scene → view (handles stay screen-sized).
    const QRectF r = m_cropRect;
    auto toView = [this, item](const QPointF &local) {
        return mapFromScene(item->mapToScene(local));
    };
    const QPoint tl = toView(r.topLeft());
    const QPoint tr = toView(r.topRight());
    const QPoint bl = toView(r.bottomLeft());
    const QPoint br = toView(r.bottomRight());
    const QPoint tm((tl.x() + tr.x()) / 2, (tl.y() + tr.y()) / 2);
    const QPoint bm((bl.x() + br.x()) / 2, (bl.y() + br.y()) / 2);
    const QPoint lm((tl.x() + bl.x()) / 2, (tl.y() + bl.y()) / 2);
    const QPoint rm((tr.x() + br.x()) / 2, (tr.y() + br.y()) / 2);

    constexpr qreal kHit = 12.0;
    auto near = [&](const QPoint &p) {
        return QLineF(viewPos, p).length() <= kHit;
    };
    if (near(tl)) {
        return CropHandle::TopLeft;
    }
    if (near(tr)) {
        return CropHandle::TopRight;
    }
    if (near(bl)) {
        return CropHandle::BottomLeft;
    }
    if (near(br)) {
        return CropHandle::BottomRight;
    }
    if (near(tm)) {
        return CropHandle::Top;
    }
    if (near(bm)) {
        return CropHandle::Bottom;
    }
    if (near(lm)) {
        return CropHandle::Left;
    }
    if (near(rm)) {
        return CropHandle::Right;
    }
    return CropHandle::None;
}

void ImageView::paintCropOverlay(QPainter &painter)
{
    if (!m_cropMode) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRect.isValid()) {
        return;
    }
    ensureCropRectValid();
    const QRectF contentScene = item->mapToScene(item->contentRect()).boundingRect();
    const QRectF cropScene = item->mapToScene(m_cropRect).boundingRect();
    const QRect contentView = QRect(mapFromScene(contentScene.topLeft()),
                                    mapFromScene(contentScene.bottomRight()))
                                  .normalized();
    const QRect cropView = QRect(mapFromScene(cropScene.topLeft()),
                                 mapFromScene(cropScene.bottomRight()))
                               .normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Dim everything outside the crop (viewport-space; covers outside content too).
    QPainterPath outer;
    outer.addRect(QRectF(viewport()->rect()));
    QPainterPath hole;
    hole.addRect(QRectF(cropView));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawPath(outer.subtracted(hole));

    // Crop frame
    painter.setBrush(Qt::NoBrush);
    QPen frame(QColor(255, 255, 255, 230), 1.5);
    frame.setCosmetic(true);
    painter.setPen(frame);
    painter.drawRect(cropView);
    QPen dash(QColor(40, 40, 40, 200), 1.0, Qt::DashLine);
    dash.setCosmetic(true);
    painter.setPen(dash);
    painter.drawRect(cropView.adjusted(1, 1, -1, -1));

    // Edge / corner handles (constant screen size).
    constexpr int kR = 5;
    auto drawHandle = [&](const QPoint &c) {
        painter.setPen(QPen(QColor(40, 40, 40), 1.0));
        painter.setBrush(QColor(255, 255, 255, 240));
        painter.drawEllipse(c, kR, kR);
    };
    const QPoint tl = cropView.topLeft();
    const QPoint tr = cropView.topRight();
    const QPoint bl = cropView.bottomLeft();
    const QPoint br = cropView.bottomRight();
    drawHandle(tl);
    drawHandle(tr);
    drawHandle(bl);
    drawHandle(br);
    drawHandle(QPoint((tl.x() + tr.x()) / 2, tl.y()));
    drawHandle(QPoint((bl.x() + br.x()) / 2, bl.y()));
    drawHandle(QPoint(tl.x(), (tl.y() + bl.y()) / 2));
    drawHandle(QPoint(tr.x(), (tr.y() + br.y()) / 2));

    Q_UNUSED(contentView);
    painter.restore();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || h == CropHandle::None) {
        return;
    }
    m_cropActiveHandle = h;
    m_cropDragStartRect = m_cropRect;
    m_cropDragStartLocal = item->mapFromScene(mapToScene(viewPos));
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || m_cropActiveHandle == CropHandle::None) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    QRectF r = m_cropDragStartRect;
    const qreal minSide = 4.0;

    switch (m_cropActiveHandle) {
    case CropHandle::Left:
        r.setLeft(qBound(cr.left(), local.x(), r.right() - minSide));
        break;
    case CropHandle::Right:
        r.setRight(qBound(r.left() + minSide, local.x(), cr.right()));
        break;
    case CropHandle::Top:
        r.setTop(qBound(cr.top(), local.y(), r.bottom() - minSide));
        break;
    case CropHandle::Bottom:
        r.setBottom(qBound(r.top() + minSide, local.y(), cr.bottom()));
        break;
    case CropHandle::TopLeft:
        r.setLeft(qBound(cr.left(), local.x(), r.right() - minSide));
        r.setTop(qBound(cr.top(), local.y(), r.bottom() - minSide));
        break;
    case CropHandle::TopRight:
        r.setRight(qBound(r.left() + minSide, local.x(), cr.right()));
        r.setTop(qBound(cr.top(), local.y(), r.bottom() - minSide));
        break;
    case CropHandle::BottomLeft:
        r.setLeft(qBound(cr.left(), local.x(), r.right() - minSide));
        r.setBottom(qBound(r.top() + minSide, local.y(), cr.bottom()));
        break;
    case CropHandle::BottomRight:
        r.setRight(qBound(r.left() + minSide, local.x(), cr.right()));
        r.setBottom(qBound(r.top() + minSide, local.y(), cr.bottom()));
        break;
    case CropHandle::None:
        break;
    }
    m_cropRect = r.normalized().intersected(cr);
    viewport()->update();
}

void ImageView::endCropHandleDrag()
{
    m_cropActiveHandle = CropHandle::None;
    ensureCropRectValid();
    viewport()->update();
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    if (!item->contentRect().contains(local)) {
        return;
    }
    m_cropRubberBanding = true;
    m_cropRubberOriginLocal = local;
    m_cropRect = QRectF(local, QSizeF(0, 0));
    viewport()->update();
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropRubberBanding) {
        return;
    }
    const QPointF local = item->mapFromScene(mapToScene(viewPos));
    const QRectF cr = item->contentRect();
    QRectF r = QRectF(m_cropRubberOriginLocal, local).normalized().intersected(cr);
    if (r.width() < 1.0) {
        r.setWidth(1.0);
    }
    if (r.height() < 1.0) {
        r.setHeight(1.0);
    }
    m_cropRect = r.intersected(cr);
    viewport()->update();
}

void ImageView::endCropRubberBand()
{
    m_cropRubberBanding = false;
    ensureCropRectValid();
    viewport()->update();
}

void ImageView::setImageModeLeftDragPan(bool on)
{
    m_imageModeLeftDragPan = on;
}

void ImageView::setSessionPosition(int index, int total, bool pulseIdentity)
{
    const bool changed = (m_sessionIndex != index || m_sessionTotal != total);
    m_sessionIndex = index;
    m_sessionTotal = total;
    // Pulse only when the session cursor actually moves (user Next/Prev, etc.).
    // Do not pulse on every statusChanged while total > 0 (AUDIT H7).
    // Slideshow auto-advance passes pulseIdentity=false.
    if (pulseIdentity && changed) {
        m_hudIdentityPulse = true;
        if (m_hudFlashTimer) {
            m_hudFlashTimer->start(1000);
        }
    }
    if (changed || m_hudVisible || m_hudFlashVisible || m_hudIdentityPulse) {
        viewport()->update();
    }
}

void ImageView::setHudVisible(bool on)
{
    if (m_hudVisible == on) {
        return;
    }
    m_hudVisible = on;
    // Progress line only paints with the pinned HUD; drive the timer accordingly.
    if (m_slideshowProgressTimer) {
        if (on && m_slideshowProgressActive && m_slideshowProgressIntervalMs > 0) {
            m_slideshowProgressTimer->start();
        } else {
            m_slideshowProgressTimer->stop();
        }
    }
    viewport()->update();
}

void ImageView::setHudFontPointSize(int pt)
{
    pt = qBound(8, pt, 48);
    if (m_hudFontPointSize == pt) {
        return;
    }
    m_hudFontPointSize = pt;
    viewport()->update();
}

void ImageView::setHudTextColor(const QColor &color)
{
    if (!color.isValid() || color == m_hudTextColor) {
        return;
    }
    m_hudTextColor = color;
    viewport()->update();
}

void ImageView::setHudPanelColor(const QColor &color)
{
    if (!color.isValid() || color == m_hudPanelColor) {
        return;
    }
    m_hudPanelColor = color;
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hudAction = action;
    m_hudDetail = detail;
    m_hudFlashVisible = true;
    m_hudIdentityPulse = true;
    if (m_hudFlashTimer) {
        m_hudFlashTimer->start(1000);
    }
    viewport()->update();
}

void ImageView::setSlideshowProgress(bool active, int intervalMs)
{
    m_slideshowProgressActive = active;
    m_slideshowProgressIntervalMs = active ? qMax(0, intervalMs) : 0;
    if (active) {
        m_slideshowProgressElapsed.start();
        if (m_hudVisible && m_slideshowProgressIntervalMs > 0 && m_slideshowProgressTimer) {
            m_slideshowProgressTimer->start();
        } else if (m_slideshowProgressTimer) {
            m_slideshowProgressTimer->stop();
        }
    } else if (m_slideshowProgressTimer) {
        m_slideshowProgressTimer->stop();
    }
    viewport()->update();
}

void ImageView::rotateLeft()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        // Snap to the previous multiple of 90° (clean right-angles)
        const qreal r = item->itemRotation();
        const qreal next = std::ceil(r / 90.0 - 1e-6) * 90.0 - 90.0;
        item->setItemRotation(next);
        rememberItemState(item);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout();
    }
    emit statusChanged();
}

void ImageView::rotateRight()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        // Snap to the next multiple of 90° (clean right-angles)
        const qreal r = item->itemRotation();
        const qreal next = std::floor(r / 90.0 + 1e-6) * 90.0 + 90.0;
        item->setItemRotation(next);
        rememberItemState(item);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout();
    }
    emit statusChanged();
}

namespace {

/** Overlap for stacking: scene AABB of content (rotation expands the box). */
bool contentOverlaps(const ImageItem *a, const ImageItem *b)
{
    if (!a || !b || a == b) {
        return false;
    }
    // Prefer AABB so partial / edge overlaps still count as a stack step.
    // Polygon-only tests were missing some overlaps and made Raise jump.
    if (a->contentSceneRect().intersects(b->contentSceneRect())) {
        return true;
    }
    const QPolygonF pa = a->contentScenePolygon();
    const QPolygonF pb = b->contentScenePolygon();
    if (pa.isEmpty() || pb.isEmpty()) {
        return false;
    }
    if (pa.intersects(pb)) {
        return true;
    }
    if (pb.containsPoint(pa.boundingRect().center(), Qt::OddEvenFill)
        || pa.containsPoint(pb.boundingRect().center(), Qt::OddEvenFill)) {
        return true;
    }
    return false;
}

/** Overlapping stack including @p item, sorted bottom → top (stable on ties). */
QList<ImageItem *> overlappingStack(ImageItem *item, const QList<ImageItem *> &all)
{
    QList<ImageItem *> layer;
    if (!item) {
        return layer;
    }
    layer.append(item);
    for (ImageItem *other : all) {
        if (other && other != item && contentOverlaps(item, other)) {
            layer.append(other);
        }
    }
    std::sort(layer.begin(), layer.end(), [](ImageItem *a, ImageItem *b) {
        if (!qFuzzyCompare(a->stackZ(), b->stackZ())) {
            return a->stackZ() < b->stackZ();
        }
        return a < b;
    });
    return layer;
}

} // namespace

void ImageView::raiseItem(ImageItem *item)
{
    if (!item || !isWorkspaceMode() || m_items.size() < 2) {
        return;
    }
    // One step: swap z with the next higher overlapping neighbour. Setting
    // z = cover.z+1 skipped intermediates when z values were sparse (e.g. 1→3
    // while 2 was an overlapping neighbour already at 3-epsilon).
    const QList<ImageItem *> layer = overlappingStack(item, m_items);
    const int idx = layer.indexOf(item);
    if (idx < 0 || idx + 1 >= layer.size()) {
        return; // already top among overlapping
    }
    ImageItem *above = layer.at(idx + 1);
    const qreal za = item->stackZ();
    const qreal zb = above->stackZ();
    if (qFuzzyCompare(za, zb)) {
        item->setStackZ(zb + 1.0);
    } else {
        item->setStackZ(zb);
        above->setStackZ(za);
    }
    emit statusChanged();
}

void ImageView::lowerItem(ImageItem *item)
{
    if (!item || !isWorkspaceMode() || m_items.size() < 2) {
        return;
    }
    const QList<ImageItem *> layer = overlappingStack(item, m_items);
    const int idx = layer.indexOf(item);
    if (idx <= 0) {
        return; // already bottom among overlapping
    }
    ImageItem *below = layer.at(idx - 1);
    const qreal za = item->stackZ();
    const qreal zb = below->stackZ();
    if (qFuzzyCompare(za, zb)) {
        item->setStackZ(zb - 1.0);
    } else {
        item->setStackZ(zb);
        below->setStackZ(za);
    }
    emit statusChanged();
}

void ImageView::raiseSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    // Raise each selection from top-most down so mutual overlaps stay stable.
    QList<ImageItem *> sel;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            sel.append(ii);
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            raiseItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) { return a->stackZ() > b->stackZ(); });
    for (ImageItem *item : sel) {
        raiseItem(item);
    }
}

void ImageView::lowerSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    QList<ImageItem *> sel;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            sel.append(ii);
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            lowerItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
    for (ImageItem *item : sel) {
        lowerItem(item);
    }
}

void ImageView::opacityUp()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() + 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(item->itemOpacity() - 0.1);
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        item->setItemOpacity(1.0);
        emit statusChanged();
    }
}

void ImageView::resetItemScale()
{
    QList<ImageItem *> targets;
    if (isWorkspaceMode()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            targets.append(item);
        } else if (ImageItem *item = primaryItem()) {
            targets.append(item);
        }
    }
    for (ImageItem *item : targets) {
        item->setItemScale(1.0, 1.0);
    }
    if (!targets.isEmpty()) {
        emit statusChanged();
        viewport()->update();
    }
}

void ImageView::resetItemRotation()
{
    QList<ImageItem *> targets;
    if (isWorkspaceMode()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            targets.append(item);
        } else if (ImageItem *item = primaryItem()) {
            targets.append(item);
        }
    }
    for (ImageItem *item : targets) {
        item->setItemRotation(0.0);
        rememberItemState(item);
    }
    if (!targets.isEmpty()) {
        emit statusChanged();
        viewport()->update();
    }
}

void ImageView::duplicateSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    QList<ImageItem *> sources;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        return;
    }

    m_scene->clearSelection();
    for (ImageItem *src : sources) {
        ImageItem *copy = createItemFromImage(src->path(), src->sourceImage());
        if (!copy) {
            continue;
        }
        copy->setItemScale(src->itemScaleX(), src->itemScaleY());
        copy->setItemRotation(src->itemRotation());
        copy->setItemHFlip(src->itemHFlip());
        copy->setItemVFlip(src->itemVFlip());
        copy->setItemOpacity(src->itemOpacity());
        copy->setStackZ(src->stackZ() + 0.01);
        // Offset so the duplicate is visible beside the original
        copy->setPos(src->pos() + QPointF(40.0, 40.0));
        copy->setSelected(true);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}

void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    if (!item) {
        return;
    }
    // DOMAIN.md ownership (Image mode):
    //   View matrix owns framing (fit / zoom / pan).
    //   Object keeps rotation and flips; this helper must never clear them.
    //   Object scale is normalized to 1 so residual Workspace scale does not
    //   fight the view transform when showing a single image.
    if (isImageMode() || m_items.size() == 1) {
        item->setItemScale(1.0);
        if (isImageMode()) {
            item->setPos(0, 0);
        }
        resetTransform();
        fitInView(item, mode);
        return;
    }
    fitInView(item, mode);
}

Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_fillMode ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
}

void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(item, 32, 32);
    }
}

QString ImageView::currentPath() const
{
    if (ImageItem *item = targetItem()) {
        return item->path();
    }
    if (ImageItem *item = primaryItem()) {
        return item->path();
    }
    return {};
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
        } else if (!m_classicPath.isEmpty()) {
            paths.append(m_classicPath);
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

QString ImageView::sessionBadgeText() const
{
    if (m_sessionTotal > 0 && m_sessionIndex >= 0 && m_sessionIndex < m_sessionTotal) {
        return tr("%1/%2").arg(m_sessionIndex + 1).arg(m_sessionTotal);
    }
    return {};
}

QString ImageView::hudFileName() const
{
    if (!m_galleryHoverPath.isEmpty()) {
        return QFileInfo(m_galleryHoverPath).fileName();
    }
    if (!m_lastLoadError.isEmpty()) {
        return QFileInfo(m_lastLoadError).fileName();
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (item) {
        return QFileInfo(item->path()).fileName();
    }
    if (!m_classicPath.isEmpty()) {
        return QFileInfo(m_classicPath).fileName();
    }
    return {};
}

QString ImageView::statusText() const
{
    if (m_zoomRegionArmed || m_zoomRegionDragging) {
        return tr("Zoom region: drag a rectangle · Esc cancels");
    }
    // Technical status only (no index badge, no bare filename — those live in
    // dedicated HUD corners). Used as secondary bottom line when the HUD is pinned.
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }

    if (!item) {
        if (!m_lastLoadError.isEmpty()) {
            return tr("Failed to load “%1”").arg(QFileInfo(m_lastLoadError).fileName());
        }
        if (!m_classicPath.isEmpty() && isImageMode()) {
            return tr("Loading…");
        }
        if (isGalleryMode()) {
            return tr("Gallery — no images");
        }
        if (isWorkspaceMode()) {
            return tr("Workspace — drop images or use Open");
        }
        return tr("Ready");
    }

    if (isMultiItemMode()) {
        const QString modeLabel = isGalleryMode() ? tr("Gallery") : tr("Workspace");
        QString text = tr("%1: %2 images  |  View zoom: %3%  |  %4×%5")
                           .arg(modeLabel)
                           .arg(m_items.size())
                           .arg(qRound(viewScale() * 100))
                           .arg(item->imageSize().width())
                           .arg(item->imageSize().height());
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr("  |  Decoding: %n", "status pending decodes", pending);
        }
        if (item->isSelected()) {
            if (qAbs(item->itemScaleX() - item->itemScaleY()) < 0.005) {
                text += tr("  |  Item: %1%  |  Rot: %2°")
                            .arg(qRound(item->itemScaleX() * 100))
                            .arg(qRound(item->itemRotation()));
            } else {
                text += tr("  |  Item: %1%×%2%  |  Rot: %3°")
                            .arg(qRound(item->itemScaleX() * 100))
                            .arg(qRound(item->itemScaleY() * 100))
                            .arg(qRound(item->itemRotation()));
            }
        }
        if (item->itemOpacity() < 0.999) {
            text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
        }
        return text;
    }

    // Image mode: zoom is view-level
    QString text = tr("%1×%2  |  Zoom: %3%  |  Rotation: %4°")
                       .arg(item->imageSize().width())
                       .arg(item->imageSize().height())
                       .arg(qRound(viewScale() * 100))
                       .arg(qRound(item->itemRotation()));
    if (item->itemHFlip() || item->itemVFlip()) {
        QStringList flips;
        if (item->itemHFlip()) {
            flips << tr("H");
        }
        if (item->itemVFlip()) {
            flips << tr("V");
        }
        text += tr("  |  Flip: %1").arg(flips.join(QLatin1Char('+')));
    }
    if (item->itemOpacity() < 0.999) {
        text += tr("  |  Opacity: %1%").arg(qRound(item->itemOpacity() * 100));
    }
    return text;
}

qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    const QPointF c = item->scenePos();
    return qRadiansToDegrees(std::atan2(scenePos.y() - c.y(), scenePos.x() - c.x()));
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
    if (isGalleryMode() && event->button() == Qt::LeftButton) {
        const QPointF scenePos = mapToScene(event->pos());
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                const QString path = item->path();
                if (!path.isEmpty()) {
                    emit galleryItemOpenRequested(path);
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
        // Image body under cursor → Image mode (reuse Gallery open signal).
        for (QGraphicsItem *gi : m_scene->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (ii->isInteractive() && m_items.contains(ii)) {
                    const QString path = ii->path();
                    if (!path.isEmpty()) {
                        emit galleryItemOpenRequested(path);
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
    if (!m_galleryHoverPath.isEmpty()) {
        m_galleryHoverPath.clear();
        viewport()->update();
    }
    QGraphicsView::leaveEvent(event);
}

