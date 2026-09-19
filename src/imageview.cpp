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
    , m_workspace(this)
    , m_image(this)
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
    m_tileCoordinator = std::make_unique<TileLoadCoordinator>(this);
    connect(m_pathRaster, &PathRasterService::rasterImproved, this,
            [this](const QString &path, int longEdge) {
                if (path.isEmpty()) {
                    return;
                }
                const QImage img = ImageCache::get(path);
                if (img.isNull()) {
                    return;
                }
                if (m_ssHud.isProgressActive()
                    && m_ss.isPhasePath(path)) {
                    onSlideshowRasterReady(path, img);
                    // SoftDisplay only at screen-fit edge (TileSynth when tiles exist).
                    if (m_pathRaster) {
                        const int target = cappedDisplayEdgeForPath(
                            path, slideshowTargetEdge());
                        const int need = target * 7 / 10;
                        if (longEdge > 0 && longEdge < need) {
                            m_pathRaster->ensure(
                                path, target, logicalSizeForPath(path),
                                PathRasterService::ClimbPolicy::SoftDisplay);
                        }
                    }
                    return;
                }
                if (isImageMode() && !m_ssHud.isProgressActive()
                    && path == classicPath()) {
                    // Event-driven ImageFocus: DisplaySurface::decide (not a
                    // quality watchdog). Soft→full via Attach* / async / climb.
                    driveImageFocusSurface();
                    emit statusChanged();
                }
                // Gallery: soft may land in ImageCache via noteDelivery while the
                // tile still shows LQIP — mirror ladderReady install.
                if (isGalleryMode()) {
                    onImagePreviewLoaded(path, img, m_loadGate.generation(),
                                         static_cast<int>(LoadAdd));
                }
                if (isWorkspaceMode()) {
                    onImagePreviewLoaded(path, img, m_loadGate.generation(),
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
        if (m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_ssHud.isPausedHud()) {
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

    m_slideshowProgressTimer = new QTimer(this);
    m_slideshowProgressTimer->setInterval(SlideshowProgressHud::kProgressTickMs); // ~30 Hz
    connect(m_slideshowProgressTimer, &QTimer::timeout, this, [this]() {
        if (m_ssHud.isProgressActive()) {
            // Pump shared path tiles for phase slides (paint uses TileLodController).
            tickPrimaryTileLod(8);
            if (viewport()) {
                viewport()->update();
            }
        } else if (m_hudPrefs.isVisible() && m_ssHud.progressIntervalMs > 0) {
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
        if (isGalleryMode() && !m_chrome.lastHoverViewPos.isNull()) {
            updateGalleryHoverAt(m_chrome.lastHoverViewPos);
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
        if (m_ssHud.isProgressActive()) {
            slideshowPhaseSurfaceTick();
        }
    });
    m_gallerySoftWatchdog->start();
}

ImageView::~ImageView()
{
    // Complete type required for unique_ptr<TileLodController> (fwd-declared in header).
    m_ss.clearTiles();

    // Invalidate any queued onImageLoaded invocations from the thread pool.
    m_loadGate.bumpGeneration();

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
        m_loadGate.clearPendingWorkspacePaths();
        gallerySoftResetAll();
        setScene(nullptr);
        delete m_scene;
        m_scene = nullptr;
    }
}



QSize ImageView::probeImageSize(const QString &path) const
{
    // Never open the source on the GUI thread (USB/NFS freeze). Cache-only or
    // neutral stand-in; scheduleImageSizeProbe / sizeReady supply the real size.
    if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        cached.isValid()) {
        return cached;
    }
    if (ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path)) {
        return ImageSizeBook::standInSquare();
    }
    return ImageSizeBook::standInNeutral();
}

void ImageView::rememberImageSize(const QString &path, const QSize &size)
{
    // HARD RULE lives in ImageSizeBook::noteDefinitive (SIZE.md identity).
    if (!m_sizeBook.noteDefinitive(path, size)) {
        return;
    }
    ThumtooCache::noteCachedSize(path, size);
}

void ImageView::rememberSizeFromDecode(const QString &path, const QImage &image)
{
    if (path.isEmpty() || image.isNull()) {
        return;
    }
    // Durable index is authoritative when present (no revalidate on GUI).
    if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
        isPositiveSize(cached)) {
        rememberImageSize(path, cached);
        return;
    }
    // Already have a definitive logical size — leave samples alone.
    if (m_sizeBook.hasDefinitive(path)) {
        return;
    }
    // Ladder / soft samples are not native identity. Probe for the real size;
    // do not write sample dimensions into the logical map.
    const int edge = qMax(image.width(), image.height());
    if (edge <= ThumtooCache::kImageLadderEdge) {
        scheduleImageSizeProbe(path);
        return;
    }
    // Larger than the image ladder max — treat as full native decode.
    rememberImageSize(path, image.size());
}

bool ImageView::isProvisionalImageSize(const QString &path) const
{
    return m_sizeBook.isProvisional(path);
}

QSize ImageView::imageSizeForPath(const QString &path)
{
    if (path.isEmpty()) {
        return ImageSizeBook::standInNeutral();
    }
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known)) {
        if (!m_sizeBook.contains(path)) {
            rememberImageSize(path, known); // install thumtoo hit into map
        }
        return known;
    }
    // Archives / multipage / embedded PDF: async probe; neutral stand-in.
    scheduleImageSizeProbe(path);
    const bool compound = ArchivePath::isArchiveRef(path) || PagePath::isPageRef(path)
        || PagePath::isPdfImageRef(path);
    // Square is only a last resort for compound refs until soft aspect or probe.
    const QSize standIn = ImageSizeBook::standInForCompoundPath(compound);
    m_sizeBook.markProvisional(path, standIn);
    return standIn;
}

QSize ImageView::layoutSizeForPath(const QString &path, const QImage &previewHint)
{
    // Prefer definitive logical size (map / thumtoo) — never soft/LQIP sample dims.
    // previewHint is display-only; using it for aspect made layout jump when LQIP
    // (wrong aspect / tiny box) was replaced by the size probe.
    Q_UNUSED(previewHint);
    const QSize known = logicalSizeForPath(path);
    if (isPositiveSize(known) && !isProvisionalImageSize(path)) {
        return known;
    }
    if (!path.isEmpty()) {
        scheduleImageSizeProbe(path);
    }
    // Provisional or definitive entry already in the book (layout needs a size).
    const QSize bookSize = m_sizeBook.known(path);
    if (!bookSize.isEmpty()) {
        return bookSize;
    }
    return imageSizeForPath(path);
}


void ImageView::primeGalleryGeometryFromCache(const QStringList &paths)
{
    // Expect warmSessionOpenMemos to have filled size memo + ImageCache LQIP.
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (!m_sizeBook.hasDefinitive(path)) {
            if (const QSize cached = ThumtooCache::cachedSize(path, /*scheduleRevalidate=*/false);
                isPositiveSize(cached)) {
                rememberImageSize(path, cached);
            }
        }
        // LQIP placeholder already in ImageCache from warmSessionOpenMemos.
        Q_UNUSED(ImageCache::has(path));
    }
}

void ImageView::scheduleImageSizeProbe(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    if (m_sizeBook.isProbeScheduled(path)) {
        return;
    }
    // Definitive size already known — provisional stand-ins must still probe.
    if (m_sizeBook.hasDefinitive(path)) {
        return;
    }
    // Never isUnsupported on the GUI (Store get_meta). scheduleProbe / worker
    // skips unsupported locators.
    // Prefer thumtoo: scheduleProbe only; Bridge::sizeReady applies the size.
    // No thread-pool Qt/vips/extract size read when the durable client is up.
    if (ThumtooCache::isAvailable()) {
        m_sizeBook.markProbeScheduled(path);
        ThumtooCache::scheduleProbe(path);
        return;
    }
    // Builds without thumtoo: native size probe on a worker.
    m_sizeBook.markProbeScheduled(path);
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path]() {
        QSize s = ImageLoader::probeSize(path);
        if (!s.isValid() || s.width() <= 0 || s.height() <= 0) {
            s = ImageSizeBook::standInNeutral();
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
            host->m_sizeBook.clearProbeScheduled(path);
            // Prefer a size already learned from a full decode — not provisional.
            if (host->m_sizeBook.hasDefinitive(path)) {
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
        // Probe is authoritative file-native size. Layout = ContentXform
        // (turns + crop), not a simple axis swap.
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentId;
        }
        WorkspaceItemState want = wantAppearanceForItem(item, sid);
        QSize layoutSize = ContentXform::layoutSize(size, want);
        if (!(layoutSize.width() > 1 && layoutSize.height() > 1)) {
            layoutSize = size;
        }
        const QSize cur = item->imageSize();
        if (cur == layoutSize) {
            continue;
        }
        item->setIntrinsicSize(layoutSize);
        any = true;
        if (isImageMode() && item == targetItem()) {
            preserveImageViewOnLogicalSizeChange(item, cur, layoutSize);
        }
    }
    if (any && isGalleryMode() && !m_layout.isFreeForm()) {
        // While the open-time size-resolve gate is active, pack once when all
        // probes settle — not on every sizeReady (avoids thrash + tiny cells).
        if (!gallerySizeResolveActive()) {
            requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
        }
    } else if (any && viewport()) {
        viewport()->update();
    }
    // Slideshow paints from path→logical, not the underlay item. When the probe
    // lands for a phase path, refresh dest aspect (and atlas if needed).
    if (m_ssHud.isProgressActive()
        && m_ss.isPhasePath(path)) {
        if (m_ss.isFromPath(path) && m_ss.hasFromImage()) {
            requestDwellAtlasRebuild();
        }
        if (m_ss.isToPath(path) && m_ss.hasToImage()) {
            requestToPhaseAtlasRebuild();
        }
        if (viewport()) {
            viewport()->update();
        }
    }
}


bool ImageView::layoutDefersPopulateUntilSizes(LayoutMode mode)
{
    // Packaged Gallery layouts need definitive aspects before the first pack.
    // Provisional 1000² cells + sizeReady re-pack was the cold-open "glitch"
    // (items flash in random positions before layout settles).
    switch (mode) {
    case LayoutMode::FreeForm:
        return false;
    default:
        return true;
    }
}

bool ImageView::startGallerySizeResolveIfNeeded(const QStringList &paths)
{
    return m_gallerySizeResolve.startIfNeeded(paths);
}

void ImageView::noteGallerySizeProbeSettled(const QString &path)
{
    m_gallerySizeResolve.noteProbeSettled(path);
}

void ImageView::cancelGallerySizeResolve()
{
    m_gallerySizeResolve.cancel();
}

bool ImageView::hasDefinitiveHostSize(const QString &path) const
{
    return m_sizeBook.hasDefinitive(path);
}

void ImageView::adoptResolvedSize(const QString &path, const QSize &size)
{
    rememberImageSize(path, size);
    applyProbedImageSize(path, size);
}

void ImageView::scheduleSizeProbe(const QString &path)
{
    scheduleImageSizeProbe(path);
}

QStringList ImageView::sizeResolvePathOrder() const
{
    return m_pathOrderBook.pathList();
}

bool ImageView::sizeResolveLayoutDefersPopulate() const
{
    return layoutDefersPopulateUntilSizes(m_layout.currentMode());
}

void ImageView::setSizeResolveProgress(const QString &title, const QString &detail)
{
    setCentreProgress(title, detail);
}

void ImageView::clearSizeResolveProgress()
{
    if (m_centreProgress.matchesTitlePrefix(tr("Resolving sizes"))) {
        clearCentreProgress();
    }
}

void ImageView::onSizeResolveGateComplete()
{
    clearCentreProgress();
    if (isGalleryMode()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    // Create tiles only now — sizes are definitive (or timed out with stand-in).
    if (m_gallerySoftBook.isDeferPopulate()) {
        m_gallerySoftBook.setDeferPopulate(false);
        ensureGalleryPlaceholders();
    } else {
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(true);
                if (!isProvisionalImageSize(item->path())) {
                    const QSize sz = layoutSizeForPath(item->path());
                    if (isPositiveSize(sz)) {
                        item->setIntrinsicSize(sz);
                    }
                }
            }
        }
        // Safety: size-resolve used to refuse createPlaceholder → empty canvas.
        if (isGalleryMode() && m_items.isEmpty() && !m_pathOrderBook.isEmpty()) {
            ensureGalleryPlaceholders();
        }
    }
    if (isGalleryMode() && !m_items.isEmpty() && !m_layout.isFreeForm()) {
        applyLayout(GalleryPackReason::EnterGallery);
        updateGalleryDecodeWindow();
        QTimer::singleShot(0, this, [this]() {
            if (isGalleryMode() && !m_items.isEmpty()) {
                updateGalleryDecodeWindow();
            }
        });
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
    emit gallerySizeResolveFinished();
}

void ImageView::onSizeResolveGateCancelled()
{
    m_gallerySoftBook.setDeferPopulate(false);
    if (isGalleryMode() && m_centreProgress.title.isEmpty()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    clearSizeResolveProgress();
    emit gallerySizeResolveFinished();
}

void ImageView::setCentreProgress(const QString &title, const QString &detail)
{
    if (title.isEmpty()) {
        clearCentreProgress();
        return;
    }
    if (!m_centreProgress.set(title, detail)) {
        return;
    }
    // Empty scene needs FullViewportUpdate or the centre panel never paints.
    // Gallery with tiles must keep BoundingRectViewportUpdate — FullViewport
    // during “Improving previews…” re-painted every item every frame and
    // undid ItemCoordinateCache scroll savings.
    if (m_items.isEmpty()) {
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearCentreProgress()
{
    if (!m_centreProgress.active()) {
        return;
    }
    m_centreProgress.clear();
    if (isGalleryMode() && !gallerySizeResolveActive()) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
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
    int n = m_loadGate.pendingWorkspaceAddCount()
          + m_loadGate.pendingRestoreCount();

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
    if (m_ssHud.isProgressActive()) {
        n += m_ss.rasterQueueCount();
        const int need = 0; // need edge checked via target below if needed
        Q_UNUSED(need);
        if (m_ss.hasFromPath()
            && ImageCache::longEdge(m_ss.fromImage) > 0
            && ImageCache::longEdge(m_ss.fromImage)
                   < (slideshowTargetEdge() * 7) / 10) {
            // Current slide still soft — count as remaining quality work once.
            if (!m_ss.rasterInflightContains(m_ss.fromPath)
                && !m_ss.rasterPendingContains(m_ss.fromPath)) {
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
    if (m_chrome.mouseInfo.valid) {
        m_chrome.clearMouseInfo();
        emit mouseInfoChanged(m_chrome.mouseInfo);
    }
    if (m_hoverEdge != EdgeZone::None) {
        clearHoverEdge();
        viewport()->update();
    }
    if (!m_gallery.hoverPath().isEmpty()) {
        m_gallery.clearHoverPath();
        viewport()->update();
    }
    if (m_ssHud.isSeekbarVisible() && !m_ssHud.isSeekDragging()) {
        m_ssHud.setSeekbarVisible(false);
        if (viewport()) {
            viewport()->update();
        }
    }
    QGraphicsView::leaveEvent(event);
}

