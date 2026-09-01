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

qreal ImageView::cardinalRotationOrZero(qreal degrees)
{
    // Image mode: always nearest 90° content orientation. Free Workspace tilt
    // (residual off the cardinal) is discarded — never shown as an arbitrary angle.
    const qreal n = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    qreal snapped = qRound(n / 90.0) * 90.0;
    if (snapped >= 360.0) {
        snapped = 0.0;
    }
    return snapped;
}

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

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    applyItemModeFlags(item);
    // Session crop survives navigation: apply only on full on-disk decodes.
    // Workspace Duplicate passes already-final pixels (possibly cropped) — do
    // not re-apply the path crop or the rect is interpreted on the wrong size.
    if (applyStoredSessionCrop) {
        // Prefer stable session-image id appearance; path map is legacy only.
        //
        // Image mode LoadReplace: the sole canvas item is the current session
        // image, so m_currentSessionId / m_sessionIndex identify it correctly.
        //
        // Gallery / Workspace LoadAdd: each tile is bound to its own session id
        // *after* creation (pendingSessionBinds). Using m_currentSessionId here
        // would bake the *navigated* image's crop into every newly decoded tile
        // when leaving Image crop for Gallery — do not apply cursor appearance
        // in multi-item modes.
        const WorkspaceItemState *app = nullptr;
        WorkspaceItemState pathFallback;
        if (isImageMode()) {
            if (m_currentSessionId != kInvalidSessionImageId) {
                if (const WorkspaceItemState *sit = m_appearance.get(m_currentSessionId)) {
                    app = &(*sit);
                }
                // Bound session image with no appearance entry = full frame, no path fallback.
            } else if (m_sessionIndex >= 0) {
                const auto sit = m_sessionSlotStates.constFind(m_sessionIndex);
                if (sit != m_sessionSlotStates.cend()) {
                    app = &(*sit);
                }
            }
            // Path map only when unbound (no session image id).
            if (!app && m_currentSessionId == kInvalidSessionImageId) {
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    pathFallback = *it;
                    app = &pathFallback;
                }
            }
        }
        if (app) {
            applySessionCrop(item, *app);
            applyContentBakes(item, *app);
            item->setSessionCrop(app->hasCrop, app->cropRect);
        }
    }
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
            // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
            clearLiveCanvas();
            ImageItem *item = createItemFromImage(path, image);
            if (!item) {
                setUpdatesEnabled(true);
                m_lastLoadError = path;
                emit statusChanged();
                return;
            }
            // Bind to the session cursor so Image-mode crop/flip targets the
            // matching Workspace slot (not every canvas instance of this path).
            if (m_currentSessionId != kInvalidSessionImageId) {
                item->setSessionId(m_currentSessionId);
            }
            if (m_sessionIndex >= 0) {
                item->setSessionIndex(m_sessionIndex);
            }
            // Never inherit Gallery/Workspace placement or scale.
            // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
            // Arbitrary Workspace rotation stays on the free-form item only.
            // Crop was applied in createItemFromImage from m_itemStates.
            item->setInteractive(false);
            item->setScaleHandlesEnabled(false);
            item->setItemScale(1.0);
            item->setPos(0, 0);
            {
                // Image mode: no Workspace placement rotation. Content 90°/flip
                // are already in pixels (createItemFromImage applies content bakes).
                item->setItemRotation(0.0);
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    // Legacy unbaked flips only if content flags not used yet.
                    if (!it->contentHFlip && !it->contentVFlip) {
                        item->setItemHFlip(it->hFlip);
                        item->setItemVFlip(it->vFlip);
                    }
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
        // Do not apply path-keyed crop — restore uses this slot's own state
        // (Workspace duplicates must not inherit another instance's crop).
        ImageItem *item = createItemFromImage(path, image, /*applyStoredSessionCrop=*/false);
        if (!item) {
            return;
        }
        // Prefer live session-image appearance over the leave-mode snapshot when
        // Image-mode edits updated m_appearance while Workspace was stashed.
        WorkspaceItemState app = state;
        if (state.sessionId != kInvalidSessionImageId) {
            item->setSessionId(state.sessionId);
            if (const WorkspaceItemState *it = m_appearance.get(state.sessionId)) {
                app = *it;
                // Keep placement from the snapshot.
                app.pos = state.pos;
                app.scale = state.scale;
                app.scaleY = state.scaleY;
                app.rotation = state.rotation;
                app.opacity = state.opacity;
                app.z = state.z;
            }
        }
        if (state.sessionIndex >= 0) {
            item->setSessionIndex(state.sessionIndex);
        }
        if (app.hasCrop) {
            applySessionCrop(item, app);
        }
        applyContentBakes(item, app);
        item->setSessionCrop(app.hasCrop, app.cropRect);
        applyState(item, app);
        if (m_layoutMode != LayoutMode::FreeForm
            && !(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
            applyLayout(GalleryPackReason::SessionMutate);
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
        // Prefer session-id appearance; path map is last-writer only for unbound.
        applyStoredAppearance(existing);
        if (isGalleryMode()) {
            // Keep pack positions stable: decoded size may differ from the probe
            // used at layout time, but a full reflow here is what made Gallery
            // "jump" on delete when a pending decode finished. Explicit layout
            // (toolbar / F5) is the only intentional repack.
            Q_UNUSED(before);
            existing->update();
        } else if (m_layoutMode != LayoutMode::FreeForm
                   && m_galleryRelayoutSuppressCount == 0) {
            applyLayout(GalleryPackReason::SessionMutate);
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
    // Bind stable session-image id / list index from the pending queue.
    for (int i = 0; i < m_pendingSessionBinds.size(); ++i) {
        if (m_pendingSessionBinds.at(i).path != path) {
            continue;
        }
        const PendingSessionBind b = m_pendingSessionBinds.takeAt(i);
        if (b.id != kInvalidSessionImageId) {
            item->setSessionId(b.id);
        }
        if (b.index >= 0) {
            item->setSessionIndex(b.index);
        }
        // Full on-disk decode — apply per-id crop/flip/rotate if any.
        applyStoredAppearance(item);
        break;
    }
    if (m_pendingSessionIndexByPath.contains(path)) {
        item->setSessionIndex(m_pendingSessionIndexByPath.take(path));
    }
    // Flags already match ViewMode via createItemFromImage / applyItemModeFlags.
    // Do not force interactive — that flashes handles in Gallery.

    // Drop position, remembered state, or empty-space placement
    if (isGalleryMode()) {
        // Packaged layouts own geometry. Never restore Workspace free-form pose
        // (arbitrary placement rotation / scale / flips) onto Gallery tiles —
        // that state lives in m_itemStates after snapshotWorkspace and would
        // otherwise reappear on every LoadAdd after entering Gallery.
        item->setItemRotation(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
        item->setItemOpacity(1.0);
        // pos/scale assigned by applyLayout() below.
    } else if (m_pendingScenePos.contains(path)) {
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
        // Gallery session-delete holds suppress so a late LoadAdd cannot repack.
        if (!(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
            applyLayout(GalleryPackReason::SessionMutate);
        }
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
    // Viewport-space chrome only — no selected-item prepareGeometryChange.
    if (viewport()) {
        viewport()->update();
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
        bakeItemFlip(item, true, false);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
}

void ImageView::flipVertical()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemFlip(item, false, true);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
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
        // Crop edits one image only; multi-select must not silently pick the first.
        if (!hasSingleCropTarget()) {
            flashHud(tr("Crop"), tr("Select a single image"));
            return;
        }
        ImageItem *item = cropTargetItem();
        if (!item || (!item->hasDecodedPixels() && item->pixmap().isNull())) {
            flashHud(tr("Crop"), tr("No image"));
            return;
        }
        cancelZoomRegion();
        // Snapshot appearance before full-image reload so Apply can be undone.
        m_cropEnterSource = item->sourceImage().copy();
        m_cropEnterState = captureState(item);
        m_cropEnterState.hasCrop = item->sessionHasCrop();
        m_cropEnterState.cropRect = item->sessionCropRect();
        m_cropEnterValid = !m_cropEnterSource.isNull();
        // Crop handles are axis-aligned in item space; free Workspace placement
        // rotation makes rubber-band and edge grips unusable. Unrotate for the
        // crop session and restore on exit.
        m_cropStashedPlacementRotation = item->itemRotation();
        m_cropHadStashedPlacement = qAbs(m_cropStashedPlacementRotation) > 0.05;
        if (m_cropHadStashedPlacement) {
            item->setItemRotation(0.0);
        }
        if (!prepareCropModeFullImage(item)) {
            m_cropEnterValid = false;
            m_cropEnterSource = QImage();
            if (m_cropHadStashedPlacement) {
                item->setItemRotation(m_cropStashedPlacementRotation);
            }
            m_cropHadStashedPlacement = false;
            flashHud(tr("Crop"), tr("Could not load full image"));
            return;
        }
        m_cropMode = true;
        m_cropActiveHandle = CropHandle::None;
        m_cropHoverHandle = CropHandle::None;
        m_cropRubberBanding = false;
        flashHud(tr("Crop mode"),
                 m_cropHadStashedPlacement
                     ? tr("Unrotated for crop · Handles · Apply · Esc")
                     : tr("Handles · Reset · Apply · Esc"));
        emit cropModeChanged(true);
        emit statusChanged();
        viewport()->update();
        return;
    }
    leaveCropModeInternal(false);
}

bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    // Always edit against the full on-disk image so the crop region can grow.
    const QImage full = ImageLoader::load(path);
    if (full.isNull()) {
        return false;
    }

    // Prior crop + content flags for *this* session image only — never path map alone.
    WorkspaceItemState app;
    bool haveApp = false;
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_currentSessionId;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = *it;
            haveApp = true;
        }
    }
    if (!haveApp && item->sessionHasCrop()) {
        app.hasCrop = true;
        app.cropRect = item->sessionCropRect();
        app.contentHFlip = item->contentHFlip();
        app.contentVFlip = item->contentVFlip();
        haveApp = true;
    }
    if (!haveApp) {
        // Last resort for unbound single-instance tiles.
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.cend()) {
            app = *it;
            haveApp = true;
        }
    }

    const QRect priorCrop = (haveApp && app.hasCrop) ? app.cropRect : QRect();
    const bool hadCrop = haveApp && app.hasCrop && !priorCrop.isEmpty();
    const bool hFlip = haveApp && (app.contentHFlip || app.hFlip);
    const bool vFlip = haveApp && (app.contentVFlip || app.vFlip);

    item->setSourceImage(full);
    // Always axis-aligned while cropping (placement was stashed in setCropMode).
    item->setItemRotation(0.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    // Re-apply content flips/quarter turns for this session image on the full frame
    // (crop draft is drawn in that space). Do not bake the crop yet.
    if (haveApp) {
        WorkspaceItemState contentOnly = app;
        contentOnly.hasCrop = false;
        contentOnly.cropRect = QRect();
        applyContentBakes(item, contentOnly);
    }
    m_cropShowingFullImage = true;

    const QRectF cr = item->contentRect();
    if (hadCrop) {
        const QSize sz = item->imageSize();
        const QRect bounds(0, 0, sz.width(), sz.height());
        const QRect src = priorCrop.intersected(bounds);
        if (src.width() >= 1 && src.height() >= 1) {
            const QPointF off = item->offset();
            int dx = src.x();
            int dy = src.y();
            int dw = src.width();
            int dh = src.height();
            if (hFlip) {
                dx = sz.width() - dx - dw;
            }
            if (vFlip) {
                dy = sz.height() - dy - dh;
            }
            m_cropRect = QRectF(dx + off.x(), dy + off.y(), dw, dh);
        } else {
            m_cropRect = cr;
        }
    } else {
        m_cropRect = cr;
    }
    ensureCropRectValid();

    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    return true;
}

void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    bool have = false;
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_currentSessionId;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = *it;
            have = true;
        }
    }
    if (!have && item->sessionHasCrop()) {
        app.hasCrop = true;
        app.cropRect = item->sessionCropRect();
        app.contentHFlip = item->contentHFlip();
        app.contentVFlip = item->contentVFlip();
        have = true;
    }
    if (!have) {
        const auto it = m_itemStates.constFind(item->path());
        if (it == m_itemStates.cend()) {
            return;
        }
        app = *it;
        have = true;
    }
    const QImage full = ImageLoader::load(item->path());
    if (!full.isNull()) {
        item->setSourceImage(full);
    }
    if (isImageMode()) {
        item->setItemRotation(0.0);
    } else {
        item->setItemRotation(app.rotation);
    }
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    applySessionCrop(item, app);
    applyContentBakes(item, app);
    item->setSessionCrop(app.hasCrop, app.cropRect);
    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_cropMode);
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    if (!src.isNull()) {
        item->setSourceImage(src);
    }
    item->setSessionCrop(state.hasCrop, state.cropRect);
    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
    applyState(item, state);
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    if (isImageMode()) {
        m_fitMode = true;
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::applyCrop()
{
    leaveCropModeInternal(true);
}

void ImageView::cancelCrop()
{
    leaveCropModeInternal(false);
}

void ImageView::applyStoredAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState fallback;
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            app = &(*it);
        }
        // Bound session image with no appearance entry = full frame.
    } else if (item->sessionIndex() >= 0) {
        const auto it = m_sessionSlotStates.constFind(item->sessionIndex());
        if (it != m_sessionSlotStates.cend()) {
            app = &(*it);
        }
    } else {
        // Path map only when unbound (no session image id).
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            fallback = *it;
            app = &fallback;
        }
    }
    if (!app) {
        return;
    }
    if (!app->hasCrop && !app->contentHFlip && !app->contentVFlip
        && app->contentQuarterTurns == 0) {
        item->setSessionCrop(false, QRect());
        return;
    }
    applySessionCrop(item, *app);
    applyContentBakes(item, *app);
    item->setSessionCrop(app->hasCrop, app->cropRect);
}

void ImageView::applySessionCrop(ImageItem *item, const WorkspaceItemState &state)
{
    SessionAppearance::applyCrop(item, state);
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    const QRectF cr = item->contentRect();
    const QRectF local = localCrop.normalized().intersected(cr);
    if (local.width() < 1.0 || local.height() < 1.0) {
        return;
    }
    const QPointF off = item->offset();
    // Crop mode always edits the full on-disk image — store absolute source rect.
    int dx = qRound(local.left() - off.x());
    int dy = qRound(local.top() - off.y());
    int dw = qMax(1, qRound(local.width()));
    int dh = qMax(1, qRound(local.height()));
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    const int iw = item->imageSize().width();
    const int ih = item->imageSize().height();
    if (item->itemHFlip()) {
        dx = iw - dx - dw;
    }
    if (item->itemVFlip()) {
        dy = ih - dy - dh;
    }
    const QRect disp(dx, dy, dw, dh);

    WorkspaceItemState s = captureState(item);
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : m_currentSessionId;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = m_appearance.get(sid)) {
            // Keep content transforms from the session-image store.
            s.contentQuarterTurns = it->contentQuarterTurns;
            s.contentHFlip = it->contentHFlip;
            s.contentVFlip = it->contentVFlip;
        }
    }
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    // Full-frame draft clears the session crop (Reset or expanded to entire image).
    const bool fullFrame =
        qAbs(local.left() - cr.left()) < 0.5
        && qAbs(local.top() - cr.top()) < 0.5
        && qAbs(local.width() - cr.width()) < 0.5
        && qAbs(local.height() - cr.height()) < 0.5;
    if (fullFrame) {
        s.hasCrop = false;
        s.cropRect = QRect();
        s.cropSourceSize = QSize();
    } else {
        s.hasCrop = true;
        s.cropRect = disp;
        s.cropSourceSize = QSize(iw, ih);
    }
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    if (sid != kInvalidSessionImageId) {
        m_appearance.set(sid, s);
    }
    // Path map remains a last-writer cache for unbound single-instance paths.
    m_itemStates.insert(item->path(), s);
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
        const bool fullFrame =
            !m_cropRect.isValid()
            || (qAbs(m_cropRect.left() - full.left()) < 0.5
                && qAbs(m_cropRect.top() - full.top()) < 0.5
                && qAbs(m_cropRect.width() - full.width()) < 0.5
                && qAbs(m_cropRect.height() - full.height()) < 0.5);
        // Record absolute crop (or clear it) while the full image is still loaded.
        recordSessionCrop(item, m_cropRect.isValid() ? m_cropRect : full);
        if (!fullFrame) {
            if (item->cropToLocalRect(m_cropRect)) {
                invalidateStashedGalleryForSession(item->sessionId());
                if (isImageMode()) {
                    m_fitMode = true;
                    fitItem(item, currentFitAspectMode());
                } else if (isWorkspaceMode()) {
                    updateWorkspaceSceneRect();
                }
                commitItemSessionEdit(item);
                // Undo: restore pre-crop-mode appearance + session crop metadata.
                if (m_undoStack && m_cropEnterValid) {
                    class CropCommand : public QUndoCommand {
                    public:
                        CropCommand(ImageView *view, ImageItem *item,
                                    const QImage &beforeSrc, const QImage &afterSrc,
                                    const WorkspaceItemState &beforeSt,
                                    const WorkspaceItemState &afterSt)
                            : m_view(view)
                            , m_item(item)
                            , m_beforeSrc(beforeSrc)
                            , m_afterSrc(afterSrc)
                            , m_beforeSt(beforeSt)
                            , m_afterSt(afterSt)
                        {
                            setText(QObject::tr("Crop"));
                        }
                        void undo() override { apply(m_beforeSrc, m_beforeSt); }
                        void redo() override { apply(m_afterSrc, m_afterSt); }
                    private:
                        void apply(const QImage &src, const WorkspaceItemState &st)
                        {
                            if (!m_view || !m_item) {
                                return;
                            }
                            m_view->applyCropAppearance(m_item, src, st);
                        }
                        ImageView *m_view;
                        ImageItem *m_item;
                        QImage m_beforeSrc;
                        QImage m_afterSrc;
                        WorkspaceItemState m_beforeSt;
                        WorkspaceItemState m_afterSt;
                    };
                    WorkspaceItemState afterSt = captureState(item);
                    afterSt.hasCrop = item->sessionHasCrop();
                    afterSt.cropRect = item->sessionCropRect();
                    m_undoStack->push(new CropCommand(
                        this, item, m_cropEnterSource, item->sourceImage().copy(),
                        m_cropEnterState, afterSt));
                }
                flashHud(tr("Cropped"),
                         QStringLiteral("%1×%2")
                             .arg(item->imageSize().width())
                             .arg(item->imageSize().height()));
            }
        } else {
            // Reset / full frame: keep full pixels; clear session crop metadata.
            invalidateStashedGalleryForSession(item->sessionId());
            if (isImageMode()) {
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
            } else if (isWorkspaceMode()) {
                updateWorkspaceSceneRect();
            }
            commitItemSessionEdit(item);
            if (m_undoStack && m_cropEnterValid
                && (m_cropEnterState.hasCrop
                    || m_cropEnterSource.size() != item->sourceImage().size())) {
                class CropCommand : public QUndoCommand {
                public:
                    CropCommand(ImageView *view, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &beforeSt,
                                const WorkspaceItemState &afterSt)
                        : m_view(view)
                        , m_item(item)
                        , m_beforeSrc(beforeSrc)
                        , m_afterSrc(afterSrc)
                        , m_beforeSt(beforeSt)
                        , m_afterSt(afterSt)
                    {
                        setText(QObject::tr("Crop reset"));
                    }
                    void undo() override { apply(m_beforeSrc, m_beforeSt); }
                    void redo() override { apply(m_afterSrc, m_afterSt); }
                private:
                    void apply(const QImage &src, const WorkspaceItemState &st)
                    {
                        if (!m_view || !m_item) {
                            return;
                        }
                        m_view->applyCropAppearance(m_item, src, st);
                    }
                    ImageView *m_view;
                    ImageItem *m_item;
                    QImage m_beforeSrc;
                    QImage m_afterSrc;
                    WorkspaceItemState m_beforeSt;
                    WorkspaceItemState m_afterSt;
                };
                WorkspaceItemState afterSt = captureState(item);
                afterSt.hasCrop = item->sessionHasCrop();
                afterSt.cropRect = item->sessionCropRect();
                m_undoStack->push(new CropCommand(
                    this, item, m_cropEnterSource, item->sourceImage().copy(),
                    m_cropEnterState, afterSt));
            }
            flashHud(tr("Crop reset"), tr("Full image"));
        }
    } else if (item && m_cropShowingFullImage) {
        // Esc / toggle off: put the previous session crop back on the canvas.
        restoreSessionCropAppearance(item);
    }
    // Restore Workspace free placement rotation after crop UI.
    if (item && m_cropHadStashedPlacement) {
        item->setItemRotation(m_cropStashedPlacementRotation);
    }
    m_cropHadStashedPlacement = false;
    m_cropStashedPlacementRotation = 0.0;
    m_cropMode = false;
    m_cropShowingFullImage = false;
    m_cropEnterValid = false;
    m_cropEnterSource = QImage();
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

namespace {
// Shared layout for Reset + Apply: prefer outside below the crop rect; only
// pull inside when the outside placement would leave the viewport.
struct CropButtonLayout {
    int x0 = 0;
    int y = 0;
    int w = 56;
    int h = 22;
    int gap = 6;
    bool valid = false;
};

CropButtonLayout cropButtonLayout(const QRectF &cropView, const QRect &viewportRect)
{
    CropButtonLayout L;
    if (!cropView.isValid() || !viewportRect.isValid()) {
        return L;
    }
    constexpr int kW = 56;
    constexpr int kH = 22;
    constexpr int kGap = 6;
    constexpr int kOutsideGap = 8;
    constexpr int kInsideInset = 8;
    constexpr int kMargin = 6;

    const int totalW = kW * 2 + kGap;
    int x0 = qRound(cropView.center().x() - totalW / 2.0);
    // Prefer outside, centred under the crop bottom edge.
    int yOutside = qRound(cropView.bottom()) + kOutsideGap;
    // Fallback: inside the crop, bottom-centred.
    int yInside = qRound(cropView.bottom()) - kH - kInsideInset;

    // Clamp horizontally into the viewport so labels stay reachable.
    const int minX = viewportRect.left() + kMargin;
    const int maxX = viewportRect.right() - kMargin - totalW;
    if (maxX >= minX) {
        x0 = qBound(minX, x0, maxX);
    } else {
        x0 = minX;
    }

    auto fullyVisible = [&](int y) {
        const QRect row(x0, y, totalW, kH);
        return viewportRect.contains(row);
    };

    int y = yOutside;
    if (!fullyVisible(yOutside)) {
        // Outside would clip — try inside the crop frame.
        if (fullyVisible(yInside)) {
            y = yInside;
        } else {
            // Still not fully visible: clamp the row into the viewport.
            y = qBound(viewportRect.top() + kMargin,
                       yOutside,
                       viewportRect.bottom() - kMargin - kH);
            // Prefer inside if that clamp is closer to the crop bottom interior.
            if (cropView.height() > kH + 2 * kInsideInset
                && viewportRect.intersects(QRect(x0, yInside, totalW, kH))) {
                const int yClampedInside = qBound(viewportRect.top() + kMargin,
                                                  yInside,
                                                  viewportRect.bottom() - kMargin - kH);
                // Use the placement that keeps the row fully on-screen.
                if (fullyVisible(yClampedInside)) {
                    y = yClampedInside;
                } else if (fullyVisible(y)) {
                    // keep clamped outside
                } else {
                    y = yClampedInside;
                }
            }
        }
    }

    L.x0 = x0;
    L.y = y;
    L.w = kW;
    L.h = kH;
    L.gap = kGap;
    L.valid = true;
    return L;
}
} // namespace

QRect ImageView::cropResetButtonView() const
{
    if (!m_cropMode || !m_cropRect.isValid() || !viewport()) {
        return QRect();
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    if (!L.valid) {
        return QRect();
    }
    return QRect(L.x0, L.y, L.w, L.h);
}

QRect ImageView::cropApplyButtonView() const
{
    if (!m_cropMode || !m_cropRect.isValid() || !viewport()) {
        return QRect();
    }
    const CropButtonLayout L = cropButtonLayout(cropRectView(), viewport()->rect());
    if (!L.valid) {
        return QRect();
    }
    return QRect(L.x0 + L.w + L.gap, L.y, L.w, L.h);
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
    // Controls sit above the crop frame (checked before edge handles).
    const QRect applyBtn = cropApplyButtonView();
    if (applyBtn.contains(viewPos)) {
        return CropHandle::Apply;
    }
    const QRect resetBtn = cropResetButtonView();
    if (resetBtn.contains(viewPos)) {
        return CropHandle::Reset;
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

    constexpr qreal kHit = 16.0;
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

    // Crop frame — amber family (distinct from single-select blue / group violet).
    painter.setBrush(Qt::NoBrush);
    QPen frame(QColor(255, 190, 40, 240), 0);
    frame.setCosmetic(true);
    frame.setWidthF(1.75);
    painter.setPen(frame);
    painter.drawRect(cropView);
    QPen dash(QColor(40, 30, 10, 180), 0, Qt::DashLine);
    dash.setCosmetic(true);
    dash.setWidthF(1.0);
    painter.setPen(dash);
    painter.drawRect(cropView.adjusted(1, 1, -1, -1));

    // Bold corner (line-arc-line) + edge bars — same language as Workspace chrome.
    const QPoint tl = cropView.topLeft();
    const QPoint tr = cropView.topRight();
    const QPoint bl = cropView.bottomLeft();
    const QPoint br = cropView.bottomRight();
    const qreal hs = 14.0;
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB,
                          CropHandle h) {
        const bool hot = (m_cropHoverHandle == h || m_cropActiveHandle == h);
        auto unit = [](QPointF v) {
            const qreal len = qHypot(v.x(), v.y());
            return len > 1e-6 ? v / len : QPointF(1, 0);
        };
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal arm = hs * (hot ? 1.55 : 1.25);
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? QColor(255, 255, 255) : QColor(255, 190, 40), 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter.setPen(hp);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        if (hot) {
            QPen glow(QColor(255, 190, 40, 200), 0);
            glow.setCosmetic(true);
            glow.setWidthF(thick * 0.55);
            glow.setCapStyle(Qt::RoundCap);
            glow.setJoinStyle(Qt::RoundJoin);
            painter.setPen(glow);
            painter.drawPath(path);
        }
    };
    // along directions point along the crop edges away from the corner.
    drawCorner(tl, QPointF(1, 0), QPointF(0, 1), CropHandle::TopLeft);
    drawCorner(tr, QPointF(-1, 0), QPointF(0, 1), CropHandle::TopRight);
    drawCorner(bl, QPointF(1, 0), QPointF(0, -1), CropHandle::BottomLeft);
    drawCorner(br, QPointF(-1, 0), QPointF(0, -1), CropHandle::BottomRight);

    auto drawEdgeBar = [&](const QPointF &mid, const QPointF &along, CropHandle h) {
        const bool hot = (m_cropHoverHandle == h || m_cropActiveHandle == h);
        auto unit = [](QPointF v) {
            const qreal len = qHypot(v.x(), v.y());
            return len > 1e-6 ? v / len : QPointF(1, 0);
        };
        const QPointF a = unit(along);
        const QPointF perp(-a.y(), a.x());
        const qreal len = hs * (hot ? 2.2 : 1.7);
        const qreal thick = hs * (hot ? 0.42 : 0.30);
        QPen hp(hot ? QColor(255, 255, 255) : QColor(120, 80, 10), 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.6 : 1.15);
        painter.setPen(hp);
        painter.setBrush(hot ? QColor(255, 220, 80, 255) : QColor(255, 190, 40, 240));
        QPolygonF bar;
        bar << mid + a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) - perp * (thick / 2)
            << mid + a * (len / 2) - perp * (thick / 2);
        painter.drawPolygon(bar);
        painter.setBrush(Qt::NoBrush);
    };
    drawEdgeBar(QPointF((tl.x() + tr.x()) / 2.0, tl.y()), QPointF(1, 0), CropHandle::Top);
    drawEdgeBar(QPointF((bl.x() + br.x()) / 2.0, bl.y()), QPointF(1, 0), CropHandle::Bottom);
    drawEdgeBar(QPointF(tl.x(), (tl.y() + bl.y()) / 2.0), QPointF(0, 1), CropHandle::Left);
    drawEdgeBar(QPointF(tr.x(), (tr.y() + br.y()) / 2.0), QPointF(0, 1), CropHandle::Right);

    // Reset / Apply: outside below crop when possible, inside if off-screen.
    auto drawTextButton = [&](const QRect &btn, CropHandle kind, const QString &label,
                              bool primary) {
        if (!btn.isValid()) {
            return;
        }
        const bool hover = (m_cropHoverHandle == kind);
        painter.setPen(QPen(primary ? QColor(120, 80, 10) : QColor(40, 40, 40), 1.0));
        if (primary) {
            // Amber primary to match crop handle language.
            painter.setBrush(hover ? QColor(255, 210, 70, 255) : QColor(240, 175, 40, 245));
        } else {
            painter.setBrush(hover ? QColor(255, 245, 220, 255) : QColor(255, 255, 255, 230));
        }
        painter.drawRoundedRect(btn, 4, 4);
        painter.setPen(primary ? QColor(40, 25, 5) : QColor(30, 30, 30));
        QFont f = painter.font();
        f.setPointSize(qMax(8, f.pointSize()));
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(btn, Qt::AlignCenter, label);
    };
    // Local QPoint names must not hide QObject::tr — use ImageView::tr.
    drawTextButton(cropResetButtonView(), CropHandle::Reset, ImageView::tr("Reset"), false);
    drawTextButton(cropApplyButtonView(), CropHandle::Apply, ImageView::tr("Apply"), true);

    // Crop size in image pixels (same coordinate space as the draft rect).
    const int cropW = qMax(1, qRound(m_cropRect.width()));
    const int cropH = qMax(1, qRound(m_cropRect.height()));
    const QString sizeLabel = QStringLiteral("%1×%2").arg(cropW).arg(cropH);
    {
        QFont f = painter.font();
        f.setPointSize(qMax(9, f.pointSize()));
        f.setBold(true);
        painter.setFont(f);
        const QFontMetrics fm(f);
        const int padX = 8;
        const int padY = 4;
        const int tw = fm.horizontalAdvance(sizeLabel);
        const int th = fm.height();
        // Prefer above the crop frame; fall back inside top edge if off-screen.
        int lx = cropView.center().x() - (tw + 2 * padX) / 2;
        int ly = cropView.top() - th - 2 * padY - 6;
        if (ly < 4) {
            ly = cropView.top() + 6;
        }
        const QRect labelBg(lx, ly, tw + 2 * padX, th + 2 * padY);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 180));
        painter.drawRoundedRect(labelBg, 4, 4);
        painter.setPen(QColor(255, 220, 120));
        painter.drawText(labelBg, Qt::AlignCenter, sizeLabel);
    }

    Q_UNUSED(contentView);
    painter.restore();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || h == CropHandle::None || h == CropHandle::Reset || h == CropHandle::Apply) {
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
    case CropHandle::Reset:
    case CropHandle::Apply:
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
        bakeItemRotate90(item, -1);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
}

void ImageView::rotateRight()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemRotate90(item, 1);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
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
        if (ImageItem *t = targetItem()) {
            targets.append(t);
        } else if (ImageItem *p = primaryItem()) {
            targets.append(p);
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
        if (ImageItem *t = targetItem()) {
            targets.append(t);
        } else if (ImageItem *p = primaryItem()) {
            targets.append(p);
        }
    }
    for (ImageItem *item : targets) {
        item->setItemRotation(0.0);
        commitItemSessionEdit(item);
    }
    if (!targets.isEmpty()) {
        viewport()->update();
    }
}

void ImageView::duplicateSelected()
{
    if (!isWorkspaceMode() && !isGalleryMode()) {
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
        // Copy current displayed pixels as-is (no second session-crop pass).
        // Value copy of current pixels + appearance (not a shared reference).
        ImageItem *copy = createItemFromImage(src->path(), src->sourceImage(),
                                              /*applyStoredSessionCrop=*/false);
        if (!copy) {
            continue;
        }
        copy->setContentHFlip(src->contentHFlip());
        copy->setContentVFlip(src->contentVFlip());
        copy->setSessionCrop(src->sessionHasCrop(), src->sessionCropRect());
        if (isWorkspaceMode()) {
            copy->setItemScale(src->itemScaleX(), src->itemScaleY());
            copy->setItemRotation(src->itemRotation());
            copy->setItemHFlip(src->itemHFlip());
            copy->setItemVFlip(src->itemVFlip());
            copy->setItemOpacity(src->itemOpacity());
            copy->setStackZ(src->stackZ() + 0.01);
            // Offset so the duplicate is visible beside the original
            copy->setPos(src->pos() + QPointF(40.0, 40.0));
        } else {
            // Gallery: upright tile; MainWindow packs after binding session ids.
            copy->setItemRotation(0.0);
            copy->setItemHFlip(false);
            copy->setItemVFlip(false);
            copy->setItemOpacity(1.0);
            copy->setPos(src->pos());
        }
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
        {
            const auto it = m_itemStates.constFind(item->path());
            if (it != m_itemStates.cend() && it->hasCrop && !it->cropRect.isEmpty()) {
                text += tr("  |  Cropped: %1×%2")
                            .arg(it->cropRect.width())
                            .arg(it->cropRect.height());
            }
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
    {
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend() && it->hasCrop && !it->cropRect.isEmpty()) {
            // cropRect is original-space size of the kept region.
            text += tr("  |  Cropped: %1×%2")
                        .arg(it->cropRect.width())
                        .arg(it->cropRect.height());
        }
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
    if (!m_galleryHoverPath.isEmpty()) {
        m_galleryHoverPath.clear();
        viewport()->update();
    }
    QGraphicsView::leaveEvent(event);
}

