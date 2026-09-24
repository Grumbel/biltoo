// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <cstdlib>
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "host/imageloader.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "display/tile_load_coordinator.h"
#include "tilelod/tile_lod_controller.hpp"
#include "session/sessionappearance.h"

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
    , m_textCtrl(this)
    , m_ownedPipeline(std::make_unique<DisplayPipelineController>(this))
    , m_displayPipeline(m_ownedPipeline.get())
    , m_size(this)
    , m_tileNeighborPrefetch(this, this)
{
    m_shell.bindView(this);
    // Phase 7 Stage 0: path/size books are owned here; appearance binds later
    // from MainWindow (SessionDocument).
    m_itemWorld.bindPathBook(&m_pathStateBook);
    m_itemWorld.bindSizeBook(&m_size.book());

    // PackOrderOverlay defaults to FollowDocument; match former empty book
    // (Explicit empty) so pack stays blank until pathOrderSetOrder / append.
    m_session.pathOrder().clearExplicit();

    m_scene = new QGraphicsScene(this);
    // BSP for viewport queries (items(rect) / hit-test). Gallery layout is not
    // rebuilt every frame — NoIndex made large sessions scan the full item
    // list on every paint/query. Duplicate/Delete still go through normal
    // removeItem; prefer index correctness over avoiding rare BSP edge cases.
    m_scene->setItemIndexMethod(QGraphicsScene::BspTreeIndex);
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
            m_gallery.updateDecodeWindow();
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
                m_displayPipeline->ensureWorkspaceQualityClimb();
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
                GUI_BUDGET("Bridge::sizeReady");
                if (path.isEmpty()) {
                    return;
                }
                m_size.book().clearProbeScheduled(path);
                const bool valid = size.isValid() && size.width() > 0 && size.height() > 0;
                if (valid) {
                    // Prefer a size already learned from a full decode.
                    if (m_size.book().hasDefinitive(path)) {
                        // Still try LQIP if tiles are blank (probe may have written LQIP).
                    } else {
                        rememberImageSize(path, size);
                        applyProbedImageSize(path, size);
                    }
                    // LQIP may already be in ImageCache (size probe callback).
                    // Still paint blank tiles; never block later soft upgrades.
                    if (isGalleryMode()) {
                        for (ImageItem *item : m_items) {
                            if (!item || item->path() != path) {
                                continue;
                            }
                            const SessionImageId sid = item->sessionId();
                            const WorkspaceItemState want =
                                m_displayPipeline->wantAppearanceForItem(item, sid);
                            const QSize lay = ContentXform::layoutSize(size, want);
                            if (isPositiveSize(lay) && lay.width() > 1) {
                                m_displayPipeline->hostSetIntrinsicSize(item, lay);
                            }
                            m_displayPipeline->tryInstallGalleryUnderlay(item);
                        }
                    }
                }
                hostGallerySizeResolve().noteProbeSettled(path, valid);
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
    // PathRasterService: created by DisplayPipelineController.
    m_displayPipeline->tileCoordinator() = std::make_unique<TileLoadCoordinator>(m_displayPipeline);
    connect(hostPathRaster(), &PathRasterService::rasterImproved, this,
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
                    if (hostPathRaster()) {
                        const int target = m_displayPipeline->cappedDisplayEdgeForPath(
                            path, m_slideshow.slideshowTargetEdge());
                        const int need = target * 7 / 10;
                        if (longEdge > 0 && longEdge < need) {
                            hostPathRaster()->ensure(
                                path, target, logicalSizeForPath(path),
                                PathRasterService::ClimbPolicy::SoftDisplay);
                        }
                    }
                    return;
                }
                if (isImageMode() && !m_slideshow.hud().isProgressActive()
                    && path == m_image.classicPath()) {
                    // Event-driven ImageFocus: DisplaySurface::decide (not a
                    // quality watchdog). Soft→full via Attach* / async / climb.
                    m_displayPipeline->driveImageFocusSurface();
                    emit statusChanged();
                }
                // Gallery: soft may land in ImageCache via noteDelivery while the
                // tile still shows LQIP — mirror ladderReady install.
                if (isGalleryMode()) {
                    m_displayPipeline->onImagePreviewLoaded(path, img, m_displayPipeline->loadGate().generation(),
                                         static_cast<int>(LoadAdd));
                }
                if (isWorkspaceMode()) {
                    m_displayPipeline->onImagePreviewLoaded(path, img, m_displayPipeline->loadGate().generation(),
                                         static_cast<int>(LoadAdd));
                }
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::ladderReady, this,
            [this](const QString &path, int maxEdge, const QImage &image) {
                m_displayPipeline->onLadderReady(path, maxEdge, image);
            });
    connect(ThumtooCache::bridge(), &ThumtooCache::Bridge::durableTilesReady, this,
            [this](const QString &path) {
                Q_UNUSED(path);
                // Warm durable discovery can fire during the Gallery size gate —
                // do not start tile I/O while probes own the Store/CPU.
                if (hostGallerySizeResolve().active()) {
                    return;
                }
                // Pyramid appeared mid-session; start tile pump if already in band.
                m_displayPipeline->tickPrimaryTileLod(8);
            });

    connect(this, &ImageView::statusChanged, this, [this]() {
        if (m_hud.appearance().isVisible() || m_hud.flash().isVisible() || m_slideshow.hud().isPausedHud()) {
            viewport()->update();
        }
    });

    m_hud.perf().enableFromEnv();
    // Layout debounce QTimer: owned by GalleryController (parented to this).
    // Colour-adjust commit QTimer: owned by ImageController (parented to this).
    m_hud.ensureFlashTimer(this, [this]() {
        m_hud.flash().clear();
        if (viewport()) {
            viewport()->update();
        }
    });

    // Slideshow progress QTimer: owned by SlideshowController (parented to this).
    m_slideshow.ensureProgressTimer();

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
    setBackgroundBrush(QBrush(m_shell.canvasBg().primaryColor()));
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
        if (isGalleryMode() && m_shell.viewport().hasHoverViewPos()) {
            m_gallery.updateGalleryHoverAt(m_shell.viewport().hoverViewPos());
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        // Debounce: every scroll pixel used to scan tiles + start pool work
        // and could peg a core while the user was only panning.
        // Gallery: decode-window settle. Image/Workspace: tile LOD + climb after
        // the scrollbar stops moving (not per pixel — that flooded cold cache).
        m_gallery.scheduleDecodeWindowRefresh(isGalleryMode()
            ? GalleryDecode::kDecodeWindowSettleMs
            : GalleryDecode::kDecodeWindowImageMs);
        // Hand pan ticks on a separate coalesced path; skip double work here.
        if (!m_shell.viewport().isPanning() && !isGalleryMode()) {
            m_displayPipeline->scheduleTileLodAfterInteraction(32);
        }
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this, refreshHover](int) {
        refreshHover();
        m_gallery.scheduleDecodeWindowRefresh(isGalleryMode()
            ? GalleryDecode::kDecodeWindowSettleMs
            : GalleryDecode::kDecodeWindowImageMs);
        if (!m_shell.viewport().isPanning() && !isGalleryMode()) {
            m_displayPipeline->scheduleTileLodAfterInteraction(32);
        }
    });

    // Recover Gallery tiles that received soft pixels but never repainted
    // (DeviceCoordinateCache + BoundingRectViewportUpdate stalls).
    m_gallery.startDecodeWatchdog();
}

void ImageView::bindSharedDisplayPipeline(DisplayPipelineController *pipeline)
{
    ASSERT_GUI_THREAD();
    Q_ASSERT(pipeline);
    if (m_displayPipeline == pipeline) {
        return;
    }
    // Drop owned pipeline; external owner keeps lifetime.
    m_ownedPipeline.reset();
    m_displayPipeline = pipeline;
}


ImageView::~ImageView()
{
    // Complete type required for unique_ptr<TileLodController> (fwd-declared in header).
    m_slideshow.phase().clearTiles();

    // Only the pipeline owner may invalidate generation / release bags / reset
    // gallery decode. A secondary host that bindSharedDisplayPipeline() must not
    // tear down the shared controller still used by the primary.
    const bool ownsPipeline = static_cast<bool>(m_ownedPipeline);

    if (ownsPipeline && m_displayPipeline) {
        // Invalidate any queued onImageLoaded invocations from the thread pool.
        m_displayPipeline->loadGate().bumpGeneration();
    }

    m_hud.stopFlashTimer();
    m_slideshow.stopProgressTimer();
    m_gallery.stopLayoutDebounceTimer();

    // Scene clear emits selectionChanged; our handler calls viewport()->update().
    // That is unsafe once ~QWidget has started deleting children — tear the
    // scene down here while ImageView is still fully constructed.
    m_gallery.discardStash();

    if (ownsPipeline && m_displayPipeline) {
        // Stage 2: detach pipeline tile bags while ImageItems are still alive.
        m_displayPipeline->releaseAllTileBags();
    }

    if (m_scene) {
        disconnect(m_scene, nullptr, this, nullptr);
        m_scene->blockSignals(true);
        m_scene->clear();
        m_items.clear();
        if (ownsPipeline && m_displayPipeline) {
            m_displayPipeline->loadGate().clearPendingWorkspacePaths();
            m_displayPipeline->galleryDecodeResetAll();
        }
        setScene(nullptr);
        delete m_scene;
        m_scene = nullptr;
    }
}
