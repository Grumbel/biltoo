// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerysoftsm.h"
#include "displayquality.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <cstdlib>
#include "archivepath.h"
#include "pagepath.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "tile_load_coordinator.h"
#include "tilelod/tile_lod_controller.hpp"
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
    , m_slideshow(this)
    , m_cropCtrl(this)
    , m_attentionCtrl(this)
    , m_workspace(this)
    , m_image(this)
    , m_displayPipeline(this)
    , m_gallerySizeResolve(this, this)
    , m_tileNeighborPrefetch(this, this)
{
    m_scene = new QGraphicsScene(this);
    // BSP indexing is fragile with frequent add/remove (Duplicate + Delete):
    // deferred paints can walk a tree that still holds freed items. Linear
    // search is fine for Workspace/Gallery counts we care about.
    m_scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(m_scene);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        // Rubber-band / programmatic selects: keep Gallery Shift-range anchor
        // on a still-selected tile when the previous anchor was dropped.
        if (isGalleryMode()) {
            if (m_gallery.selectionAnchor()
                && !m_gallery.selectionAnchor()->isSelected()) {
                ImageItem *next = nullptr;
                for (QGraphicsItem *gi : m_scene->selectedItems()) {
                    if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                        next = ii;
                        break;
                    }
                }
                m_gallery.setSelectionAnchor(next);
            }
            // Selection frames are scene-space overlays (drawForeground).
            // Do not invalidate ItemCoordinateCache on every select — that was
            // the old DeviceCoordinate path and defeated scroll caching.
            if (viewport()) {
                viewport()->update();
            }
            // Refresh interest snapshot so FocusFull tracks the new Primary.
            // Sticky zoom is Image-mode only; Gallery uses ensureVisible for
            // keep-selection-in-view.
            updateGalleryDecodeWindow();
        }
        // Workspace: Primary = first selected; Near = remaining selection.
        if (isWorkspaceMode()) {
            QStringList primary;
            QStringList near;
            for (QGraphicsItem *gi : m_scene->selectedItems()) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    const QString path = ii->path();
                    if (path.isEmpty()) {
                        continue;
                    }
                    if (primary.isEmpty()) {
                        primary.append(path);
                    } else {
                        near.append(path);
                    }
                }
            }
            if (!primary.isEmpty()) {
                (void)ThumtooCache::setInterest(
                    near, {}, ThumtooCache::kBatchOverviewEdge,
                    ThumtooCache::kGalleryLadderEdge, primary,
                    ThumtooCache::kImageLadderEdge);
                ensureWorkspaceQualityClimb();
            }
        }
        emit statusChanged();
        emit canvasSelectionChanged();
    });
    m_undoStack = new QUndoStack(this);
    qRegisterMetaType<QImage>("QImage");
    qRegisterMetaType<quint64>("quint64");

    // Durable native size from thumtoo (GUI thread via Qt Executor).
    // Invalid/empty size still clears probe scheduling and advances the Gallery
    // size-resolve gate (failed request_size must not stick forever).
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::sizeReady, this,
            [this](const QString &path, const QSize &size) {
                if (path.isEmpty()) {
                    return;
                }
                m_sizeBook.clearProbeScheduled(path);
                const bool valid = size.isValid() && size.width() > 0 && size.height() > 0;
                if (valid) {
                    // Prefer a size already learned from a full decode.
                    if (m_sizeBook.hasDefinitive(path)) {
                        // Still try LQIP if tiles are blank (probe may have written LQIP).
                    } else {
                        rememberImageSize(path, size);
                        applyProbedImageSize(path, size);
                    }
                    // LQIP may already be in ImageCache (size probe callback).
                    // Still paint blank tiles; never block later soft upgrades.
                    if (isGalleryMode()) {
                        QImage lqip = ImageCache::get(path);
                        if (lqip.isNull()) {
                            lqip = ThumtooCache::cachedLqipImage(path);
                            if (!lqip.isNull()) {
                                ImageCache::put(path, lqip);
                            }
                        }
                        if (!lqip.isNull()) {
                            for (ImageItem *item : m_items) {
                                if (!item || item->path() != path) {
                                    continue;
                                }
                                if (item->hasDisplayPixels()) {
                                    continue;
                                }
                                // Geometry first — LQIP must not pack from 1×1.
                                const SessionImageId sid = item->sessionId();
                                const WorkspaceItemState want =
                                    wantAppearanceForItem(item, sid);
                                const QSize lay = ContentXform::layoutSize(size, want);
                                if (isPositiveSize(lay) && lay.width() > 1) {
                                    item->setIntrinsicSize(lay);
                                }
                                installDisplayPixels(item, lqip,
                                                     SessionAppearance::PixelKind::SoftPreview,
                                                     sid);
                            }
                        }
                    }
                }
                noteGallerySizeProbeSettled(path);
            });

    // Soft preview: install better ladder pixels; clear inflight when matched.
        connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderProvenance, this,
            [this](const QString &path, int, int) {
                if (path.isEmpty()) {
                    return;
                }
                ImageItem *item = targetItem();
                if (!item) {
                    item = primaryItem();
                }
                if (item && item->path() == path) {
                    refreshStatus();
                }
            });
    m_pathRaster = new PathRasterService(this);
    m_displayPipeline.tileCoordinator() = std::make_unique<TileLoadCoordinator>(this);
    connect(m_pathRaster, &PathRasterService::rasterImproved, this,
            [this](const QString &path, int longEdge) {
                if (path.isEmpty()) {
                    return;
                }
                const QImage img = ImageCache::get(path);
                if (img.isNull()) {
                    return;
                }
                if (m_slideshow.hud().isProgressActive()
                    && m_slideshow.phase().isPhasePath(path)) {
                    m_slideshow.onSlideshowRasterReady(path, img);
                    // SoftDisplay only at screen-fit edge (TileSynth when tiles exist).
                    if (m_pathRaster) {
                        const int target = cappedDisplayEdgeForPath(
                            path, m_slideshow.slideshowTargetEdge());
                        const int need = target * 7 / 10;
                        if (longEdge > 0 && longEdge < need) {
                            m_pathRaster->ensure(
                                path, target, logicalSizeForPath(path),
                                PathRasterService::ClimbPolicy::SoftDisplay);
                        }
                    }
                    return;
                }
                if (isImageMode() && !m_slideshow.hud().isProgressActive()
                    && path == classicPath()) {
                    // Event-driven ImageFocus: DisplaySurface::decide (not a
                    // quality watchdog). Soft→full via Attach* / async / climb.
                    driveImageFocusSurface();
                    emit statusChanged();
                }
                // Gallery: soft may land in ImageCache via noteDelivery while the
                // tile still shows LQIP — mirror ladderReady install.
                if (isGalleryMode()) {
                    onImagePreviewLoaded(path, img, m_displayPipeline.loadGate().generation(),
                                         static_cast<int>(LoadAdd));
                }
                if (isWorkspaceMode()) {
                    onImagePreviewLoaded(path, img, m_displayPipeline.loadGate().generation(),
                                         static_cast<int>(LoadAdd));
                }
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            &ImageView::onLadderReady);
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::durableTilesReady, this,
            [this](const QString &path) {
                Q_UNUSED(path);
                // Pyramid appeared mid-session; start tile pump if already in band.
                tickPrimaryTileLod(8);
            });

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_slideshow.hud().isPausedHud()) {
            viewport()->update();
        }
    });

    m_perf.enableFromEnv();
    m_hudFlashTimer = new QTimer(this);
    m_hudFlashTimer->setSingleShot(true);
    m_layoutDebounceTimer = new QTimer(this);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(LayoutDebounce::kIntervalMs);
    connect(m_layoutDebounceTimer, &QTimer::timeout, this, [this]() {
        GalleryPackReason reason = GalleryPackReason::ContentChange;
        if (isGalleryMode() && !m_layout.isFreeForm()
            && m_layoutDebounce.take(&reason)) {
            applyLayout(reason);
        }
    });
    // Colour sliders fire every tick — durable SQLite + filmstrip bake are deferred.
    m_colorAdjustCommitTimer = new QTimer(this);
    m_colorAdjustCommitTimer->setSingleShot(true);
    m_colorAdjustCommitTimer->setInterval(ColorAdjustCommit::kIntervalMs);
    connect(m_colorAdjustCommitTimer, &QTimer::timeout, this, [this]() {
        flushColorAdjustCommit();
    });
    connect(m_hudFlashTimer, &QTimer::timeout, this, [this]() {
        m_hudFlash.clear();
        viewport()->update();
    });

    m_slideshow.progressTimer() = new QTimer(this);
    m_slideshow.progressTimer()->setInterval(SlideshowProgressHud::kProgressTickMs); // ~30 Hz
    connect(m_slideshow.progressTimer(), &QTimer::timeout, this, [this]() {
        if (m_slideshow.hud().isProgressActive()) {
            // Pump shared path tiles for phase slides (paint uses TileLodController).
            tickPrimaryTileLod(8);
            if (viewport()) {
                viewport()->update();
            }
        } else if (m_hudPrefs.isVisible() && m_slideshow.hud().hasProgressInterval()) {
            if (viewport()) {
                viewport()->update();
            }
        }
    });

    setRenderHint(QPainter::SmoothPixmapTransform, true);
    // OpenGL viewport — overlays must use drawForeground (see paintEvent).
    setViewport(new QOpenGLWidget);
    setAcceptDrops(true);
    if (viewport()) {
        viewport()->setAcceptDrops(true);
    }
    setDragMode(QGraphicsView::NoDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setBackgroundBrush(QBrush(m_canvasBg.primaryColor()));
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
        if (isGalleryMode() && m_chrome.hasHoverViewPos()) {
            updateGalleryHoverAt(m_chrome.hoverViewPos());
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        // Debounce: every scroll pixel used to scan all tiles + start pool
        // work and could peg a core while the user was only panning.
        // Gallery soft install needs a responsive window while LQIP→soft climbs.
        scheduleGalleryDecodeWindowRefresh(isGalleryMode()
            ? GallerySoft::kDecodeWindowSettleMs
            : GallerySoft::kDecodeWindowImageMs);
        // Image/Workspace deep zoom: timer may be stopped after coverage;
        // scrollbar drag (or pan setValue) must re-issue visible cells.
        // Hand pan already ticks; skip when m_chrome.isPanning() to avoid double work.
        if (!m_chrome.isPanning() && !isGalleryMode()) {
            tickPrimaryTileLod(4);
        }
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        scheduleGalleryDecodeWindowRefresh(isGalleryMode()
            ? GallerySoft::kDecodeWindowSettleMs
            : GallerySoft::kDecodeWindowImageMs);
        if (!m_chrome.isPanning() && !isGalleryMode()) {
            tickPrimaryTileLod(4);
        }
    });

    // Recover Gallery tiles that received soft pixels but never repainted
    // (DeviceCoordinateCache + BoundingRectViewportUpdate stalls).
    m_gallerySoftWatchdog = new QTimer(this);
    m_gallerySoftWatchdog->setInterval(GallerySoft::kWatchdogIntervalMs);
    connect(m_gallerySoftWatchdog, &QTimer::timeout, this, [this]() {
        if (isGalleryMode()) {
            gallerySoftWatchdogTick();
        }
        // ImageFocus is event-driven only (rasterImproved / load / resize climb).
        // Slideshow phase buffers: DisplaySurface::decide while transition is live.
        if (m_slideshow.hud().isProgressActive()) {
            m_slideshow.slideshowPhaseSurfaceTick();
        }
    });
    m_gallerySoftWatchdog->start();
}

ImageView::~ImageView()
{
    // Complete type required for unique_ptr<TileLodController> (fwd-declared in header).
    m_slideshow.phase().clearTiles();

    // Invalidate any queued onImageLoaded invocations from the thread pool.
    m_displayPipeline.loadGate().bumpGeneration();

    if (m_hudFlashTimer) {
        m_hudFlashTimer->stop();
    }
    if (m_slideshow.progressTimer()) {
        m_slideshow.progressTimer()->stop();
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
        m_displayPipeline.loadGate().clearPendingWorkspacePaths();
        gallerySoftResetAll();
        setScene(nullptr);
        delete m_scene;
        m_scene = nullptr;
    }
}



void ImageView::requestDebouncedGalleryPack(GalleryPackReason reason)
{
    m_layoutDebounce.arm(reason);
    if (!m_layoutDebounceTimer) {
        applyLayout(reason);
        return;
    }
    m_layoutDebounceTimer->start();
}

int ImageView::pendingDecodeCount() const
{
    // Remaining work overview — not concurrent inflight. Counting only inflight
    // flickered 1↔0 as each soft job finished before the next was claimed.
    int n = m_displayPipeline.loadGate().pendingWorkspaceAddCount()
          + m_displayPipeline.loadGate().pendingRestoreCount();

    if (isGalleryMode()) {
        // Gallery: blanks still need LQIP. LQIP-only is intentional underlay
        // (tiles own sharpness) — do not count as remaining soft work.
        QSet<QString> blankPaths;
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (!item->hasDisplayPixels()) {
                blankPaths.insert(item->path());
            }
        }
        n += blankPaths.size();
    } else if (isWorkspaceMode()) {
        // Workspace may still climb PreferCache for soft+ samples.
        QSet<QString> weakPaths;
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            const int edge = item->displayPixelLongEdge();
            if (!item->hasDisplayPixels()
                || edge <= DisplayQuality::kLqipMaxEdge) {
                weakPaths.insert(item->path());
            }
        }
        n += weakPaths.size();
    }

    // Slideshow preload queue (inflight + pending neighbours).
    if (m_slideshow.hud().isProgressActive()) {
        n += m_slideshow.phase().rasterQueueCount();
        const int need = 0; // need edge checked via target below if needed
        Q_UNUSED(need);
        if (m_slideshow.phase().hasFromPath()
            && ImageCache::longEdge(m_slideshow.phase().fromImageRef()) > 0
            && ImageCache::longEdge(m_slideshow.phase().fromImageRef())
                   < (m_slideshow.slideshowTargetEdge() * 7) / 10) {
            // Current slide still soft — count as remaining quality work once.
            if (!m_slideshow.phase().rasterInflightContains(m_slideshow.phase().fromPathRef())
                && !m_slideshow.phase().rasterPendingContains(m_slideshow.phase().fromPathRef())) {
                ++n;
            }
        }
    }
    return n;
}















































Qt::AspectRatioMode ImageView::currentFitAspectMode() const
{
    return m_framing.aspectMode();
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
                m_itemInteract.beginHandleDrag(item, captureState(item));
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
    if (m_chrome.hasMouseInfo()) {
        m_chrome.clearMouseInfo();
        emit mouseInfoChanged(m_chrome.currentMouseInfo());
    }
    if (m_hoverEdge != EdgeZone::None) {
        clearHoverEdge();
        viewport()->update();
    }
    if (!m_gallery.hoverPath().isEmpty()) {
        m_gallery.clearHoverPath();
        viewport()->update();
    }
    if (m_slideshow.hud().isSeekbarVisible() && !m_slideshow.hud().isSeekDragging()) {
        m_slideshow.hud().setSeekbarVisible(false);
        if (viewport()) {
            viewport()->update();
        }
    }
    QGraphicsView::leaveEvent(event);
}

