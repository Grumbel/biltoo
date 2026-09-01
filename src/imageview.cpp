// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
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
#include <QPointer>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
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

