// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaypipelinecontroller.h"
#include "display/displaypipeline_jobs.h"

#include "display/tile_load_coordinator.h"
#include "tilelod/tile_lod_registry.hpp"

#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "display/displayedgepolicy.h"
#include "display/pathrasterservice.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "display/lqipdisplaypolicy.h"
#include "util/biltoo_thread.h"

#include "host/imageloader.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "color/coloradjust.h"
#include "item/imagesizebook.h"
#include "view/viewtransform.h"
#include "util/biltoo_logging.h"
#include "util/ttfp_trace.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QMutex>
#include <QWaitCondition>
#include <QMutexLocker>
#include <memory>
#include <QPointer>
#include <QMetaObject>
#include <cstdlib>
#include <cstdio>

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QVarLengthArray>
#include <QTimer>
#include <QObject>


DisplayPipelineController::DisplayPipelineController(DisplayPipelineHost *host)
    : m_host(host)
{
    Q_ASSERT(m_host);
    // Parent to host shell so lifetime tracks the ImageView that owns the pipeline.
    m_pathRaster = new PathRasterService(m_host->hostObject());
}

void DisplayPipelineController::setActiveHost(DisplayPipelineHost *host)
{
    ASSERT_GUI_THREAD();
    Q_ASSERT(host);
    m_host = host;
}

DisplayPipelineController::~DisplayPipelineController()
{
    releaseAllTileBags();
}

void DisplayPipelineController::ensureWorkspaceQualityClimb()
{
    ASSERT_GUI_THREAD();
    if (!m_host->isWorkspaceMode() || !m_host->hostPathRaster() || !m_host->canvasScene()) {
        return;
    }
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : m_host->canvasScene()->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            targets.append(ii);
        }
    }
    if (targets.isEmpty()) {
        // No selection: climb all on-canvas items (bounded).
        int n = 0;
        for (ImageItem *ii : m_host->liveItems()) {
            if (!ii || ii->path().isEmpty()) {
                continue;
            }
            targets.append(ii);
            if (++n >= 8) {
                break;
            }
        }
    }
    // Tile LOD for workspace items that need past soft max.
    tickPrimaryTileLod(8);

    for (ImageItem *ii : targets) {
        if (!ii) {
            continue;
        }
        const QString path = ii->path();
        if (path.isEmpty()) {
            continue;
        }
        if (m_host->hostCrop().isCropDraftLockedPath(path)) {
            continue;
        }
        // tileLodWanted: tiles own display — skip PreferCache (global tick already
        // ran above). Durable-only still allows SoftDisplay PreferCache underlay
        // until LOD wants (same as tilesOwnDisplay(wanted, false) / countDurable=false).
        if (ii->tileLodWanted()) {
            continue;
        }
        // Always measure need after the current view transform (zoom/pan).
        const int needEdge = itemOnScreenNeedEdge(ii, /*allowHighRes=*/true);
        const int have = ii->displayPixelLongEdge();
        if (needEdge > 0 && have > 0 && DisplayEdgePolicy::coversEdge(have, needEdge)) {
            continue;
        }
        const bool pending = m_host->hostPathRaster()->isClimbPending(path);
        syncItemDisplaySurface(ii, -1, pending);
        const DisplaySurface::SurfaceId sid =
            static_cast<DisplaySurface::SurfaceId>(ii->displaySurfaceId());
        if (sid != DisplaySurface::kInvalidSurfaceId) {
            displaySurfaces().setNeed(sid, needEdge);
        }
        DisplaySurface::Action act =
            (sid != DisplaySurface::kInvalidSurfaceId)
                ? displaySurfaces().evaluate(sid)
                : DisplaySurface::decide(
                      displaySurfaceStateForItem(ii, -1, pending));
        if (act.type == DisplaySurface::ActionType::ScheduleClimb
            && act.climbNeedEdge < needEdge) {
            act.climbNeedEdge = needEdge;
        }
        // Decide may return None while still short (stale settle / Full shortfall).
        // Force PathRaster escalate so Soft→Prefer→Full continues on zoom-in.
        if (act.type == DisplaySurface::ActionType::None
            && needEdge > 0 && !DisplayEdgePolicy::coversEdge(have, needEdge) && !pending) {
            act.type = DisplaySurface::ActionType::ScheduleClimb;
            act.climbNeedEdge = needEdge;
        }
        {
            const auto pol =
                ThumtooCache::hasDurableTilesKnown(path)
                    ? PathRasterService::ClimbPolicy::SoftDisplay
                    : PathRasterService::ClimbPolicy::EscalateToFull;
            (void)applyDisplaySurfaceAction(ii, act, QImage(), needEdge, pol);
        }
        if (!DisplayEdgePolicy::coversEdge(ii->displayPixelLongEdge(), needEdge)
            && (m_host->hostPathRaster()->isGaveUp(path)
                || (!m_host->hostPathRaster()->isClimbPending(path)
                    && act.type == DisplaySurface::ActionType::ScheduleClimb))) {
            // PreferCache plateau short of need — native only when no durable tiles.
            if (!ThumtooCache::hasDurableTilesKnown(path)
                && (m_host->hostPathRaster()->isGaveUp(path)
                    || !m_host->hostPathRaster()->isClimbPending(path))) {
                scheduleImageModeNativeDecodeOnce(path);
            }
        }
    }
}

void DisplayPipelineController::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty() || m_host->hostGalleryDecodeBook().hasImageModeNativeDecode(path)) {
        return;
    }
    m_host->hostGalleryDecodeBook().markImageModeNativeDecode(path);
    const quint64 gen = loadGate().generation();
    const QPointer<QObject> life(m_host->hostObject());
    DisplayPipelineController *pipe = this;
    QThreadPool::globalInstance()->start([life, pipe, path, gen]() {
        ASSERT_NOT_GUI_THREAD();
        const QImage decoded = ImageLoader::load(path);
        if (!life || !pipe) {
            return;
        }
        QMetaObject::invokeMethod(
            life.data(),
            [life, pipe, path, decoded, gen]() {
                if (!life || !pipe || !pipe->host()) {
                    return;
                }
                if (!decoded.isNull()) {
                    ImageCache::put(path, decoded);
                }
                DisplayPipelineHost *h = pipe->host();
                if (h->isImageMode()) {
                    if (gen != pipe->loadGate().generation()) {
                        return;
                    }
                    if (!decoded.isNull()) {
                        (void)pipe->tryInstallImageModeSample(path, decoded);
                    }
                } else if (h->isWorkspaceMode() && !decoded.isNull()) {
                    pipe->onImagePreviewLoaded(
                        path, decoded, pipe->loadGate().generation(),
                        static_cast<int>(ImageView::LoadAdd));
                }
            },
            Qt::QueuedConnection);
    });
}
bool DisplayPipelineController::tickTilesIfOwnDisplay(const QString &path,
                                                      bool countDurable)
{
    if (path.isEmpty()) {
        return false;
    }
    // PreferCache skip budgets a short primary tick (not the full coordinator default).
    constexpr int kOwnDisplayTickBudget = 12;
    const bool durable =
        countDurable && ThumtooCache::hasDurableTilesKnown(path);
    auto owns = [&](ImageItem *ii) {
        return ii
            && DisplayEdgePolicy::tilesOwnDisplay(ii->tileLodWanted(), durable);
    };
    // Fast path: Image underlay (target/primary) before full liveItems scan.
    if (owns(imageModeItemForPath(path))) {
        tickPrimaryTileLod(kOwnDisplayTickBudget);
        return true;
    }
    for (ImageItem *ii : m_host->liveItems()) {
        if (ii && ii->path() == path && owns(ii)) {
            tickPrimaryTileLod(kOwnDisplayTickBudget);
            return true;
        }
    }
    // No live item for path but durable pyramid known (prefetch / filmstrip).
    if (durable) {
        tickPrimaryTileLod(kOwnDisplayTickBudget);
        return true;
    }
    return false;
}

void DisplayPipelineController::requestEscalateClimb(const QString &path, int wantEdge)
{
    if (!m_host->hostPathRaster() || path.isEmpty() || m_host->hostSlideshow().hud().isNavHot()) {
        return;
    }
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }
    // Tiles own display: tileLodWanted or known durable pyramid — no PreferCache.
    if (tickTilesIfOwnDisplay(path)) {
        return;
    }
    const int edge = cappedDisplayEdgeForPath(
        path, wantEdge > 0 ? wantEdge : ThumtooCache::kImageLadderEdge);
    // Slideshow + durable tiles: SoftDisplay (PreferCache/TileSynth) only —
    // EscalateToFull native decode is the CPU storm on prepared libraries.
    const auto policy =
        (m_host->hostSlideshow().hud().isProgressActive() && ThumtooCache::hasDurableTilesKnown(path))
            ? PathRasterService::ClimbPolicy::SoftDisplay
            : PathRasterService::ClimbPolicy::EscalateToFull;
    biltooLoadDbg("escalateClimb(service) path=%s edge=%d policy=%d",
                  qPrintable(QFileInfo(path).fileName()), edge,
                  static_cast<int>(policy));
    m_host->hostPathRaster()->ensure(path, edge, m_host->logicalSizeForPath(path), policy);
}
void DisplayPipelineController::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    if (path.isEmpty() || m_host->hostSlideshow().hud().isNavHot() || !m_host->hostPathRaster()) {
        return;
    }
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }
    if (m_host->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    // Tiles own display once wanted or durable pyramid is known — no PreferCache.
    if (tickTilesIfOwnDisplay(path)) {
        return;
    }
    if (!sample.isNull() && sampleCoversNativeLogical(path, sample)) {
        return;
    }

    const int need = imageModeOnScreenNeedEdge();
    const int have = sample.isNull() ? 0 : ImageCache::longEdge(sample);
    // Cold path only (no durable tiles): climb to on-screen need, not soft-512 habit.
    const int escalated = DisplayEdgePolicy::escalateClimbTo(
        ThumtooCache::kBatchOverviewEdge, need);
    const int climbTo = DisplayEdgePolicy::climbEdgeIfNeeded(
        have, need, ThumtooCache::kBatchOverviewEdge,
        cappedDisplayEdgeForPath(path, escalated));
    if (climbTo <= 0) {
        return;
    }
    biltooLoadDbg("imageModeClimb(service) path=%s climbTo=%d have=%d need=%d cold",
                  qPrintable(QFileInfo(path).fileName()), climbTo, have, need);
    m_host->hostPathRaster()->ensure(path, climbTo, m_host->logicalSizeForPath(path),
                         PathRasterService::ClimbPolicy::EscalateToFull);
    // Full is async. If terminal or nothing pending and still short, host native
    // (only when no durable tiles — prepared libs use TileSynth / tile LOD).
    if (!ThumtooCache::hasDurableTilesKnown(path)
        && !DisplayEdgePolicy::coversEdge(m_host->hostPathRaster()->haveEdge(path), climbTo)
        && (m_host->hostPathRaster()->isGaveUp(path) || !m_host->hostPathRaster()->isClimbPending(path))) {
        scheduleImageModeNativeDecodeOnce(path);
    }
}

void DisplayPipelineController::installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                             const QImage &image,
                                             SessionAppearance::PixelKind kind)
{
    if (!item || image.isNull()) {
        return;
    }
    // Same rules as every other attach: accept → materialize → attachDisplaySample.
    installDisplayPixels(item, image, kind, resolveItemSessionId(item));
    m_host->hostSessionId().clearLastLoadError();
    m_host->rememberSizeFromDecode(path, image);
    if (m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
}


int DisplayPipelineController::cappedDisplayEdgeForPath(const QString &path, int wantEdge) const
{
    int nativeLong = 0;
    const QSize logical = m_host->logicalSizeForPath(path);
    if (isPositiveSize(logical) && !m_host->hostSizeBook().isProvisional(path)) {
        nativeLong = ContentXform::longEdge(logical);
    }
    return DisplayEdgePolicy::cappedDisplayEdge(wantEdge, nativeLong);
}

bool DisplayPipelineController::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    const int incoming = ImageCache::longEdge(image);
    int nativeLong = 0;
    bool nativeKnown = false;
    const QSize logical = m_host->logicalSizeForPath(path);
    if (isPositiveSize(logical) && !m_host->hostSizeBook().isProvisional(path)) {
        nativeLong = ContentXform::longEdge(logical);
        nativeKnown = nativeLong > 0;
    }
    return DisplayEdgePolicy::sampleCoversNative(
        incoming, nativeLong, nativeKnown, ThumtooCache::kBatchOverviewEdge,
        ThumtooCache::kImageLadderEdge);
}

bool DisplayPipelineController::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    if (!m_host->isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    // @p image is host-raw (soft job, ladder, quality job). Single materialize
    // in installDisplayPixels — soft stand-in + async full when multi-MP want.
    const SessionAppearance::PixelKind kind = pixelKindForImageModeSample(path, image);
    const bool ok = tryInstallImageModeSampleBaked(path, image, kind);
    // Decide soft→async / climb from the new host edge (event-driven).
    // Nav-hot: install only — driveImageFocusSurface is a no-op while hot.
    driveImageFocusSurface();
    if (ok && m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
    return ok;
}

bool DisplayPipelineController::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    // Name is historical: @p image is host-raw. installDisplayPixels materializes.
    if (!m_host->isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return false;
    }
    if (ImageItem *cur = imageModeItemForPath(path)) {
        if (canAcceptDisplaySample(cur, image, kind)) {
            installImageModeSampleInPlace(cur, path, image, kind);
            biltooLoadDbg("tryInstall OK path=%s kind=%d edge=%d",
                          qPrintable(QFileInfo(path).fileName()),
                          int(kind), ImageCache::longEdge(image));
        } else {
            biltooLoadDbg("tryInstall REJECT path=%s kind=%d edge=%d have=%d decoded=%d",
                          qPrintable(QFileInfo(path).fileName()),
                          int(kind), ImageCache::longEdge(image),
                          cur->displayPixelLongEdge(),
                          cur->hasDecodedPixels() ? 1 : 0);
        }
        // Climb while soft or short of native.
        const int incoming = ImageCache::longEdge(image);
        const int painted = cur->displayPixelLongEdge();
        const bool noUpgrade = incoming > 0 && painted > 0 && incoming <= painted;
        if (kind == SessionAppearance::PixelKind::SoftPreview
            || !sampleCoversNativeLogical(path, image)) {
            if (!(noUpgrade && m_host->hostPathRaster() && m_host->hostPathRaster()->isGaveUp(path))) {
                ensureImageModeQualityClimb(path, image);
            } else {
                // Terminal PreferCache/Full shortfall at same edge as painted —
                // still need host native when on-screen need exceeds that.
                const int need = imageModeOnScreenNeedEdge();
                if (need > painted) {
                    scheduleImageModeNativeDecodeOnce(path);
                }
            }
        }
        return true;
    }
    // First install for this path: SoftPreview uses pending-tile path so
    // FullSource-only m_view->createItemFromImage is not forced on a soft sample.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        installImageModePendingTile(path, image);
        ensureImageModeQualityClimb(path, image);
        m_host->notifyStatusChanged();
    } else {
        installImageModeReplaceItem(path, image);
        if (!sampleCoversNativeLogical(path, image)) {
            ensureImageModeQualityClimb(path, image);
        }
    }
    return true;
}

void DisplayPipelineController::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    GUI_BUDGET("DisplayPipeline::onLadderReady");
    ASSERT_GUI_THREAD();
    if (path.isEmpty()) {
        return;
    }
    // Central climb policy + ImageCache put. Slideshow installs via
    // PathRasterService::rasterImproved (no second direct call).
    if (m_host->hostPathRaster()) {
        m_host->hostPathRaster()->noteDelivery(path, maxEdge, image);
        // PreferCache BestAvailable / Full escalate is PathRasterService policy
        // (docs/THUMTOO_HOST_CONTRACT.md). Do not recover ad-hoc here.
    } else {
        if (!image.isNull()) {
            ImageCache::put(path, image);
        }
        if (m_host->hostSlideshow().hud().isProgressActive() && !image.isNull()) {
            m_host->hostSlideshow().onSlideshowRasterReady(path, image);
        }
    }

    // Image mode: soft→sharp when full ImageLoader::load missed or is still
    // in flight. ladderReady used to return early for non-Gallery, so PDF /
    // page / archive PreferCache deliveries left the view stuck on the soft
    // thumbnail forever.
    if (m_host->isImageMode() && !image.isNull() && !m_host->hostSlideshow().hud().isProgressActive()) {
        upgradeImageModeFromLadder(path, maxEdge, image);
    }

    // Workspace: was falling through to early return without installing samples.
    if (m_host->isWorkspaceMode() && !image.isNull()) {
        applyWorkspaceLadderReady(path, maxEdge, image);
    }

    // Crop may be open in Image or Workspace on a provisional sample.
    if (!image.isNull()) {
        m_host->hostCrop().maybeUpgradeCropFullRaster(path, image);
    }

    // PreferCache/FocusFull may have co-built durable tiles; wake tile LOD only
    // when a live item already wants tiles (countDurable=false — skip every
    // soft underlay delivery that has not entered the tile band).
    if (m_host->isImageMode() || m_host->isWorkspaceMode() || m_host->isGalleryMode()) {
        (void)tickTilesIfOwnDisplay(path, /*countDurable=*/false);
    }

    if (!m_host->isGalleryMode()) {
        return;
    }
    applyGalleryLadderReady(path, maxEdge, image);
}


void DisplayPipelineController::upgradeImageModeFromLadder(const QString &path, int maxEdge,
                                           const QImage &image)
{
    // ladderReady Image-mode path: same install policy as completeLoadReplace.
    if (path.isEmpty() || image.isNull() || path != m_host->hostImage().classicPath()) {
        return;
    }
    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        const int req = maxEdge > 0 ? maxEdge : ImageCache::longEdge(image);
        fprintf(stderr,
                "biltoo/image: ladderReady UPGRADE path=%s req=%d got=%dx%d\n",
                qPrintable(QFileInfo(path).fileName()), req, image.width(),
                image.height());
    }
    // Ladder samples are host-raw. installDisplayPixels materializes want
    // (soft stand-in ≤512 + async full for multi-MP). Never bake here and
    // put the result into ImageCache — that double-applied crop.
    Q_UNUSED(maxEdge);
    (void)tryInstallImageModeSample(path, image);
}


void DisplayPipelineController::applyGalleryLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    if (!m_host->isGalleryMode() || path.isEmpty()) {
        return;
    }
    Q_UNUSED(maxEdge);
    // Gallery accepts LQIP only. Larger soft samples stay in ImageCache for
    // filmstrip / Image mode — not painted onto Gallery cells.
    if (!image.isNull()
        && ImageCache::longEdge(image) <= DisplayQuality::kLqipMaxEdge) {
        ImageCache::put(path, image);
        for (ImageItem *item : m_host->liveItems()) {
            if (!item || item->path() != path || item->hasDisplayPixels()) {
                continue;
            }
            installDisplayPixels(item, image,
                                 SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }
        if (m_host->viewportWidget()) {
            m_host->viewportWidget()->update();
        }
    }

    if (GalleryDecodeState *st = m_host->hostGalleryDecodeBook().find(path)) {
        st->terminal = true;
        st->have = GalleryDecode::maxHave(st->have, galleryHaveEdgeFromItems(path, nullptr));
    }

    m_host->hostGallery().scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSliceMs);
    m_host->hostGallery().scheduleStatusRefresh(GalleryDecode::kStatusRefreshMs);
}



void DisplayPipelineController::applyWorkspaceLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (!m_host->isWorkspaceMode() || path.isEmpty() || image.isNull()) {
        return;
    }
    Q_UNUSED(maxEdge);
    // Same soft/display install as Gallery tiles — Workspace items share paths.
    onImagePreviewLoaded(path, image, loadGate().generation(),
                         static_cast<int>(ImageView::LoadAdd));
    ensureWorkspaceQualityClimb();
}


void DisplayPipelineController::maybeClimbImageModePixelsForView()
{
    // Zoom / resize: PreferCache climbs when on-screen need exceeds painted.
    // Do not start Display@ladder while soft is still missing — that races the
    // soft 512 job and is what THUMTOO_DEBUG showed as need=2048 decoded=0.
    if (!m_host->isImageMode() || m_host->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    ImageItem *item = imageModeItemForPath(m_host->hostImage().classicPath());
    if (!item) {
        item = m_host->targetItem();
    }
    if (!item || item->path().isEmpty()) {
        return;
    }
    const QString path = item->path();
    // Crop draft freezes the sample — do not schedule soft↔full climb.
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }

    // Tiles own display (tileLodWanted or durable pyramid): tick LOD, skip PreferCache.
    // Matches requestEscalateClimb / ensureImageModeQualityClimb (tickTilesIfOwnDisplay).
    if (tickTilesIfOwnDisplay(path)) {
        driveImageFocusSurface();
        return;
    }

    const int need = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
    const int have = item->displayPixelLongEdge();
    if (have <= 0) {
        // Soft / LQIP not installed yet — ensureImageModeQualityClimb runs
        // after soft lands; PreferCache waits for that.
        return;
    }
    if (need <= 0 || DisplayEdgePolicy::coversEdge(have, need)) {
        return;
    }

    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/image: view climb path=%s have=%d need=%d decoded=%d\n",
                qPrintable(QFileInfo(path).fileName()), have, need,
                item->hasDecodedPixels() ? 1 : 0);
    }

    // Moderate zoom still on soft band: PathRaster Soft→PreferCache→Full.
    requestEscalateClimb(path, need);
    // Soft matching want + large host → ScheduleAsyncMaterialize via decide.
    driveImageFocusSurface();
}

DisplaySurface::State DisplayPipelineController::displaySurfaceStateForItem(const ImageItem *item,
                                                            int hostLongEdge,
                                                            bool climbPending) const
{
    DisplaySurface::State ds;
    if (!item) {
        return ds;
    }
    ds.climbPending = climbPending;
    ds.hostLongEdge = hostLongEdge >= 0
        ? hostLongEdge
        : (item->path().isEmpty()
               ? 0
               : ImageCache::longEdge(ImageCache::get(item->path())));
    ds.want = ContentXform::Value::fromState(
        wantAppearanceForItem(item, item->sessionId()));
    if (item->hasDisplayPixels()) {
        ds.haveDisplayEdge = item->displayPixelLongEdge();
        ds.attachedKind = item->hasDecodedPixels()
            ? DisplaySurface::AttachedKind::FullSource
            : DisplaySurface::AttachedKind::SoftPreview;
        if (m_host->itemHasAppliedContentXform(item)) {
            ds.applied = m_host->itemAppliedContentXform(item);
        }
    }
    if (m_host->isImageMode()) {
        // ImageFocus: on-screen (window) need only. Forcing need ≥ file native
        // jumped straight to Full and skipped Soft→Prefer progressive installs.
        // Zoom / 1:1 raises on-screen need; climb then escalates.
        const int need = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
        ds.needEdge = cappedDisplayEdgeForPath(item->path(), need);
    } else if (m_host->isGalleryMode()) {
        ds.needEdge = galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
    } else if (m_host->isWorkspaceMode()) {
        ds.needEdge = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
    }
    return ds;
}


bool DisplayPipelineController::applyDisplaySurfaceAction(ImageItem *item,
                                          const DisplaySurface::Action &act,
                                          const QImage &hostSample,
                                          int fallbackNeedEdge,
                                          PathRasterService::ClimbPolicy climbPolicy)
{
    if (!item) {
        return false;
    }
    using AT = DisplaySurface::ActionType;
    if (act.type == AT::None) {
        return false;
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        return false;
    }
    if (act.type == AT::ScheduleClimb) {
        // Gallery never PreferCache/soft climb — LQIP + tiles only.
        if (m_host->isGalleryMode() || !m_host->hostPathRaster()) {
            return false;
        }
        // Image key-repeat: never ensure per skipped path (IMAGE_MODE_NAV_SOFT).
        if (m_host->hostSlideshow().hud().isNavHot() && m_host->isImageMode()) {
            return false;
        }
        const int need = act.climbNeedEdge > 0 ? act.climbNeedEdge : fallbackNeedEdge;
        if (need > 0) {
            m_host->hostPathRaster()->ensure(path, need, m_host->logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::ScheduleAsyncMaterialize) {
        if (m_host->isGalleryMode()) {
            return false;
        }
        if (m_host->hostSlideshow().hud().isNavHot() && m_host->isImageMode()) {
            return false;
        }
        scheduleAsyncHostRematerialize(
            path, item->sessionId(),
            wantAppearanceForItem(item, item->sessionId()));
        // Intermediate Prefer host is baking async; keep Soft→Prefer→Full climb
        // when on-screen need is still above host (Workspace zoom-in).
        const int need = fallbackNeedEdge > 0 ? fallbackNeedEdge : 0;
        if (m_host->hostPathRaster() && need > 0
            && !DisplayEdgePolicy::coversEdge(item->displayPixelLongEdge(), need)
            && !DisplayEdgePolicy::coversEdge(ImageCache::longEdge(ImageCache::get(path)), need)) {
            m_host->hostPathRaster()->ensure(path, need, m_host->logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::AttachSoft || act.type == AT::AttachFull) {
        QImage host = hostSample;
        if (host.isNull()) {
            host = ImageCache::get(path);
        }
        if (host.isNull()) {
            return false;
        }
        auto kind = (act.type == AT::AttachSoft)
            ? SessionAppearance::PixelKind::SoftPreview
            : SessionAppearance::PixelKind::FullSource;
        if (!canAcceptDisplaySample(item, host, kind)) {
            // Gallery LQIP tile: force SoftPreview when host is a real upgrade.
            const int shown = item->displayPixelLongEdge();
            const int hostEdge = ImageCache::longEdge(host);
            if (!(m_host->isGalleryMode()
                  && shown <= DisplayQuality::kLqipMaxEdge
                  && hostEdge > shown)) {
                return false;
            }
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        const QSize before = item->imageSize();
        if (m_host->isImageMode()) {
            installImageModeSampleInPlace(item, path, host, kind);
        } else {
            installDisplayPixels(item, host, kind, item->sessionId());
            item->update();
            if (m_host->canvasScene()) {
                m_host->canvasScene()->update(item->sceneBoundingRect());
            }
        }
        const bool sizeChanged = (item->imageSize() != before);
        if (act.type == AT::AttachSoft) {
            syncItemDisplaySurface(item, ImageCache::longEdge(host),
                m_host->hostPathRaster() && m_host->hostPathRaster()->isClimbPending(path));
            const DisplaySurface::SurfaceId sid =
                static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
            const DisplaySurface::Action again =
                (sid != DisplaySurface::kInvalidSurfaceId)
                    ? displaySurfaces().evaluate(sid)
                    : DisplaySurface::decide(
                          displaySurfaceStateForItem(
                              item, ImageCache::longEdge(host), false));
            if (again.type == AT::ScheduleAsyncMaterialize) {
                scheduleAsyncHostRematerialize(
                    path, item->sessionId(),
                    wantAppearanceForItem(item, item->sessionId()));
            } else if (again.type == AT::ScheduleClimb && m_host->hostPathRaster()
                       && !m_host->isGalleryMode()) {
                const int need = again.climbNeedEdge > 0 ? again.climbNeedEdge
                                                         : fallbackNeedEdge;
                if (need > 0) {
                    m_host->hostPathRaster()->ensure(path, need, m_host->logicalSizeForPath(path),
                                         climbPolicy);
                }
            }
        }
        return sizeChanged;
    }
    return false;
}


void DisplayPipelineController::driveImageFocusSurface()
{
    if (!m_host->isImageMode() || m_host->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    // Key-repeat: install soft only. evaluate() → ScheduleClimb / async bake
    // would pathRaster->ensure every skipped path (bypassed requestEscalateClimb
    // nav-hot guard). Settle loadImage drives the surface once.
    if (m_host->hostSlideshow().hud().isNavHot()) {
        return;
    }
    syncImageFocusSurfaceState();
    if (imageFocusSurfaceRef() == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    const DisplaySurface::Binding *b =
        displaySurfaces().binding(imageFocusSurfaceRef());
    if (!b) {
        return;
    }
    const DisplaySurface::Action action = displaySurfaces().evaluate(imageFocusSurfaceRef());
    const QString path = b->path;
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = m_host->primaryItem();
    if (!item || item->path() != path) {
        return;
    }
    const SessionImageId sid = resolveItemSessionId(item, b->sessionId);
    Q_UNUSED(sid);
    {
        const auto pol =
            ThumtooCache::hasDurableTilesKnown(path)
                ? PathRasterService::ClimbPolicy::SoftDisplay
                : PathRasterService::ClimbPolicy::EscalateToFull;
        (void)applyDisplaySurfaceAction(
            item, action, QImage(),
            cappedDisplayEdgeForPath(path, 0), pol);
    }
}


void DisplayPipelineController::registerItemDisplaySurface(ImageItem *item)
{
    if (!item || item->path().isEmpty()) {
        return;
    }
    unregisterItemDisplaySurface(item);
    DisplaySurface::Kind kind = DisplaySurface::Kind::GalleryTile;
    if (m_host->isWorkspaceMode()) {
        kind = DisplaySurface::Kind::WorkspaceItem;
    } else if (m_host->isImageMode()) {
        kind = DisplaySurface::Kind::ImageFocus;
    }
    const DisplaySurface::SurfaceId id = displaySurfaces().bind(
        kind, item->path(), item->sessionId());
    item->setDisplaySurfaceId(id);
    // Stage 2: pipeline bag ready before first paint/tick (avoids local fallback).
    ensureTileBag(item);
}


void DisplayPipelineController::unregisterItemDisplaySurface(ImageItem *item)
{
    if (!item) {
        return;
    }
    const qint64 sid = item->displaySurfaceId();
    if (sid == 0) {
        return;
    }
    displaySurfaces().unbind(static_cast<DisplaySurface::SurfaceId>(sid));
    item->setDisplaySurfaceId(0);
}


void DisplayPipelineController::syncItemDisplaySurface(ImageItem *item, int hostLongEdge,
                                       bool climbPending)
{
    if (!item) {
        return;
    }
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    const DisplaySurface::SurfaceId id =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    if (id == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    const DisplaySurface::State ds =
        displaySurfaceStateForItem(item, hostLongEdge, climbPending);
    displaySurfaces().setNeed(id, ds.needEdge);
    displaySurfaces().setFrozen(id, ds.frozen);
    displaySurfaces().setHostLongEdge(id, ds.hostLongEdge);
    displaySurfaces().setClimbPending(id, ds.climbPending);
    displaySurfaces().setWant(id, ds.want);
    displaySurfaces().setAttached(id, ds.attachedKind, ds.haveDisplayEdge,
                                  ds.applied);
}

bool DisplayPipelineController::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    if (!item || pixels.isNull()) {
        return false;
    }
    const int incoming = ImageCache::longEdge(pixels);
    if (incoming <= 0) {
        return false;
    }
    // Soft must not demote FullSource (also enforced by decide Soft path).
    if (kind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        return false;
    }
    // Gallery underlay: ThumbHash LQIP (≤96) or container EMB (≤320). Never
    // soft/HOST whole-frame PreferCache. request_size often returns EMB >96 —
    // rejecting those left every cell as a neutral rectangle (2392).
    if (m_host->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview
        && incoming > DisplayQuality::kEmbeddedUnderlayMaxEdge) {
        return false;
    }
    if (!item->hasDisplayPixels()) {
        return true; // blank: LQIP/EMB SoftPreview (gated above)
    }
    if (m_host->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        // Larger underlay within the EMB band; never soft climb past that.
        const int shown = item->displayPixelLongEdge();
        if (incoming > shown
            && incoming <= DisplayQuality::kEmbeddedUnderlayMaxEdge) {
            return true;
        }
        return LqipDisplayPolicy::galleryAcceptsLqipUpgrade(
            shown, incoming, DisplayQuality::kLqipMaxEdge);
    }
    DisplaySurface::State ds = displaySurfaceStateForItem(item, incoming, false);
    if (m_host->hostCrop().isCropDraftLockedItem(item) || m_host->hostCrop().isCropDraftLockedPath(item->path())) {
        ds.frozen = true;
    }
    const DisplaySurface::Action act = DisplaySurface::decide(ds);
    using AT = DisplaySurface::ActionType;
    if (act.type == AT::None) {
        return false;
    }
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        return act.type == AT::AttachSoft
            || act.type == AT::ScheduleAsyncMaterialize;
    }
    // FullSource: accept when decide wants full attach or async full bake.
    return act.type == AT::AttachFull
        || act.type == AT::ScheduleAsyncMaterialize
        || act.type == AT::AttachSoft;
}

void DisplayPipelineController::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    GUI_BUDGET("DisplayPipeline::installDisplayPixels");
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Gallery: pixels never precede definitive size (layout authority).
    if (m_host->isGalleryMode() && !path.isEmpty()) {
        const ImageSizeBook &book = m_host->hostSizeBook();
        if (book.isFailed(path)) {
            return;
        }
        if (!book.hasDefinitive(path)) {
            return;
        }
    }
    // Crop draft owns the live sample — ladder/async must not replace it
    // (store want still has crop → wrong bake; soft↔full thrash).
    if (m_host->hostCrop().isCropDraftLockedItem(item) || m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }
    if (!canAcceptDisplaySample(item, pixels, kind)) {
        return;
    }
    if (!pixels.isNull()) {
        TtfpTrace::noteFirstPixels("installDisplayPixels");
    }
    const QSize layoutBefore = item->imageSize();

    // Resolve session id (Image-mode soft path often passes invalid sid).
    sid = resolveItemSessionId(item, sid);
    // Image mode: never materialize under a sid whose document path ≠ underlay path.
    if (m_host->isImageMode() && sid != kInvalidSessionImageId && !path.isEmpty()) {
        if (SessionDocument *doc = m_host->sessionDocument()) {
            const int docIdx = doc->indexOfId(sid);
            if (docIdx >= 0 && doc->paths().at(docIdx) != path) {
                qCritical("installDisplayPixels: fixup sid=%lld for path=%s "
                          "(document maps id to %s)",
                          static_cast<long long>(sid),
                          qPrintable(path),
                          qPrintable(doc->paths().at(docIdx)));
                sid = kInvalidSessionImageId;
                const SessionImageId itemSid = item->sessionId();
                if (itemSid != kInvalidSessionImageId) {
                    const int itemIdx = doc->indexOfId(itemSid);
                    if (itemIdx < 0 || doc->paths().at(itemIdx) == path) {
                        sid = itemSid;
                    }
                }
                if (sid == kInvalidSessionImageId) {
                    const int byPath = doc->indexOfPathPreferId(path);
                    if (byPath >= 0) {
                        sid = doc->idAt(byPath);
                    }
                }
            }
        }
    }
    // Image underlay: do NOT seed XDG here. Path XDG seed on first Image open
    // wrote orient into contentBake when only Workspace Placement existed —
    // host stayed unrotated on Workspace, Image materialize then rotated
    // "unrotated" images. Content ops for Image come only from explicit
    // contentBake/crop (user edit). Gallery may still seed via wantAppearance.
    WorkspaceItemState appearance;
    if (m_host->isImageMode() && sid != kInvalidSessionImageId) {
        appearance = m_host->sessionAppearanceValue(sid);
        // Placement-only durable row is not content orient.
        appearance = SessionAppearance::orientAuthorityWant(
            m_host->itemWorld().hasContentOrient(sid), appearance);
        if (qEnvironmentVariableIsSet("BILTOO_MODE_DEBUG")) {
            fprintf(stderr,
                    "biltoo/orient Image install sid=%lld turns=%d bake=%d crop=%d path=%s\n",
                    static_cast<long long>(sid),
                    appearance.contentQuarterTurns,
                    m_host->itemWorld().hasContentBake(sid) ? 1 : 0,
                    m_host->itemWorld().hasCrop(sid) ? 1 : 0,
                    qPrintable(QFileInfo(path).fileName()));
        }
    } else {
        seedSessionAppearanceFromState(sid, path);
        appearance = wantAppearanceForItem(item, sid);
    }

    // Host cache is unoriented. Every ladder/decode sample that enters here is
    // host-raw (Gallery, Image, Workspace). Display-ready stash soft never
    // enters this function — pendingTile attaches it via attachDisplaySample.
    //
    // Invariant: for session-bound tiles, attach only materializeDisplay(host,
    // store want). Never attach host under a content want. Never set applied
    // xform unless the attached pixels match that bake.
    const bool wantBake =
        SessionAppearance::hasContentAppearance(appearance)
        || !appearance.colorAdjust.isIdentity();
    if (!path.isEmpty()) {
        // Incoming is always unoriented host — keep ImageCache pure.
        ImageCache::put(path, pixels);
    }

    // raw → optional gallery soft clamp → materializeDisplay → attach.
    QImage pixelsForDisplay = pixels;
    if (m_host->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        pixelsForDisplay = DisplayEdgePolicy::clampSoftForCell(
            pixels,
            galleryDisplayEdgeForItem(item, /*allowHighRes=*/true),
            ThumtooCache::kFilmstripLadderEdge);
    }
    QImage display = pixelsForDisplay;
    SessionAppearance::PixelKind attachKind = kind;
    bool scheduleFullBake = false;
    if (wantBake) {
        // Key-repeat: never schedule async rematerialize per skipped path —
        // settle loadImage will bake once for the final index.
        const bool navHot = m_host->hostSlideshow().hud().isNavHot() && m_host->isImageMode();
        // Nav-hot: tighter clamp so materializeDisplay stays cheap under hold.
        const int maxGui = navHot
            ? ContentXform::materializePreviewEdge()
            : ContentXform::kGuiMaterializeMaxEdge;
        const int hostEdge = ImageCache::longEdge(pixelsForDisplay);
        // Multi-MP host cannot materialize on the GUI thread. Soft stand-in
        // (clamp ≤512 + SoftPreview bake) keeps crop/orient visible now;
        // full-resolution bake is scheduled async. Never attach raw host.
        if (hostEdge > maxGui) {
            pixelsForDisplay = ImageCache::clampToMaxEdge(pixelsForDisplay, maxGui);
            attachKind = SessionAppearance::PixelKind::SoftPreview;
            // Only escalate to full async bake when the caller asked for FullSource.
            scheduleFullBake = !navHot
                && (kind == SessionAppearance::PixelKind::FullSource);
        }
        const int edge = ImageCache::longEdge(pixelsForDisplay);
        if (edge <= 0) {
            if (!navHot) {
                scheduleAsyncHostRematerialize(path, sid, appearance);
            }
            return;
        }
        if (edge > maxGui) {
            // Clamp failed oddly — still do not attach host under want.
            if (!navHot) {
                scheduleAsyncHostRematerialize(path, sid, appearance);
            }
            return;
        }
        display = SessionAppearance::materializeDisplay(
            pixelsForDisplay, appearance, attachKind);
        if (display.isNull()) {
            if (!navHot) {
                scheduleAsyncHostRematerialize(path, sid, appearance);
            }
            return;
        }
        // Soft attach is ignored while FullSource is present.
        if (attachKind == SessionAppearance::PixelKind::SoftPreview
            && item->hasDecodedPixels()) {
            item->clearDecodedPixels();
        }
    }
    const QSize sizeBeforeAttach = item->imageSize();
    attachDisplaySample(item, display, appearance, attachKind);
    if (scheduleFullBake) {
        scheduleAsyncHostRematerialize(path, sid, appearance);
    }
    // Soft→layout may change aspect; keep Image view scale continuous.
    if (m_host->isImageMode() && item == m_host->targetItem()
        && sizeBeforeAttach != item->imageSize()
        && sizeBeforeAttach.width() > 1 && sizeBeforeAttach.height() > 1) {
        m_host->preserveImageViewOnLogicalSizeChange(item, sizeBeforeAttach, item->imageSize());
    } else if (m_host->isImageMode() && m_host->canvasScene() && m_host->liveItems().size() == 1) {
        m_host->canvasScene()->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    // Do NOT emit sessionAppearanceChanged from decode/install (filmstrip is
    // selection-coupled). Soft ladder upgrades must not rewrite the strip.

    // Gallery reflow only when layout geometry actually changed.
    if (m_host->isGalleryMode() && item->imageSize() != layoutBefore) {
        m_host->requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
}

void DisplayPipelineController::installImageModePendingTile(const QString &path, const QImage &preview)
{

    if (!m_host->isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the m_view->viewport with dwell/live blits. Pending tile used to
    // m_view->clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed. Underlay is hidden for the whole show.
    if (m_host->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }

    bool displayReady = false;
    QImage pixels = resolveImageModePendingPixels(path, preview, &displayReady);
    biltooLoadDbg("pendingTile path=%s soft=%dx%d cache=%d displayReady=%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height(),
                  ImageCache::has(path) ? 1 : 0, displayReady ? 1 : 0);

    // Cold path: no soft/LQIP yet. Within the *same* path, keep the prior frame
    // until soft arrives (avoids a flash on PreferCache gaps). Different path
    // (session switch / ←→): never keep the previous file's pixels under a new
    // contentRect — that is the wrong-pixels stretch. Layout without pixels is
    // fine (blank/placeholder at the correct aspect); only the old sample is not.
    if (pixels.isNull()) {
        if (m_host->liveItems().size() == 1) {
            ImageItem *item = m_host->liveItems().first();
            const bool pathChanged = item->path() != path;
            item->setPath(path);
            bindImageModeSessionCursor(item);
            if (pathChanged) {
                // Soft OR full — hasDecodedPixels is full-only and left prior soft
                // in place so canAccept rejected the next path's smaller LQIP.
                if (item->hasDisplayPixels()) {
                    item->clearDecodedPixels();
                }
                m_host->clearLiveContentMeta(item);
                m_host->syncLiveColorFromState(item, ColorAdjustments{});
                // Content layout (ItemWorld / XDG orient), not file-native alone.
                const SessionImageId sid = m_host->hostSessionId().currentIdValue();
                const QSize sz = m_host->contentLayoutSize(path, sid);
                if (isPositiveSize(sz)) {
                    hostSetIntrinsicSize(item, sz);
                    m_host->syncImageModeSceneRect(item);
                }
                // Soft PreferCache encode is removed for Image underlay
                // (LQIP + tiles only). Nav-hot: no IPC — settle loadImage probes
                // size and issues tiles once. Do not scheduleSoftPixels here.
                if (!m_host->hostSlideshow().hud().isNavHot() && ThumtooCache::isAvailable()) {
                    ThumtooCache::scheduleProbe(path);
                }
                if (m_host->viewportWidget()) {
                    m_host->viewportWidget()->update();
                }
                biltooLoadDbg(
                    m_host->hostSlideshow().hud().isNavHot()
                        ? "pendingTile DEFER blank path=%s (nav-hot, placeholder)"
                        : "pendingTile DEFER blank path=%s (placeholder, probe size)",
                    qPrintable(QFileInfo(path).fileName()));
            } else {
                biltooLoadDbg("pendingTile DEFER empty soft path=%s keep prior frame",
                              qPrintable(QFileInfo(path).fileName()));
            }
            return;
        }
        // First image ever / post-clearLiveCanvas: create underlay and frame it.
        // Do NOT call prepareImageModeCanvas here — that resets sceneRect to
        // empty after the item exists and leaves Image mode looking blank until
        // a later soft install (or forever if soft/LQIP never arrives).
        const SessionImageId sid = m_host->hostSessionId().currentIdValue();
        const QSize sz = m_host->contentLayoutSize(path, sid);
        ImageItem *item = createPlaceholderItem(path, isPositiveSize(sz) ? sz : QSize(1, 1));
        if (item) {
            bindImageModeSessionCursor(item);
            resetImageModeItemPlacement(item);
            m_host->syncImageModeSceneRect(item);
            m_host->applyImageModeFraming(item);
            if (m_host->viewportWidget()) {
                m_host->viewportWidget()->update();
            }
        } else {
            biltooLoadDbg("pendingTile PLACEHOLDER FAILED path=%s mode=%d defer=%d",
                          qPrintable(QFileInfo(path).fileName()),
                          m_host->isImageMode() ? 1 : 0,
                          m_host->hostGalleryDecodeBook().isDeferPopulate() ? 1 : 0);
        }
        biltooLoadDbg("pendingTile PLACEHOLDER empty soft path=%s items=%d sz=%dx%d",
                      qPrintable(QFileInfo(path).fileName()),
                      m_host->itemCount(),
                      sz.width(), sz.height());
        return;
    }

    // Content layout size (ItemWorld); soft sample never defines geometry.
    const SessionImageId layoutSid = m_host->hostSessionId().currentIdValue();
    const QSize sz = m_host->contentLayoutSize(path, layoutSid);

    // Fast path: reuse the single Image-mode item.
    // Do NOT m_host->setUpdatesEnabled(false) — that defers soft paint until after the
    // whole key handler (chrome + climb schedule); user never sees the soft.
    ImageItem *item = nullptr;
    if (m_host->liveItems().size() == 1) {
        item = m_host->liveItems().first();
    }
    if (item) {
        const QSize sizeBefore = item->imageSize();
        // Capture only when navigating to a different file — same-path soft→HQ
        // upgrades must not replace a user pan with a stale pre-frame anchor.
        const bool pathChanged = (item->path() != path);
        if (pathChanged) {
            m_host->captureStickyPanAnchor(item);
        }
        item->setPath(path);
        bindImageModeSessionCursor(item);
        // Path change: drop prior sample AND content chrome. wantAppearanceForItem
        // merges ItemWorld Crop/ContentBake (live item fallback) when the store slot is empty;
        // leaking the previous image's crop into the new soft is the ←/→ stretch.
        if (pathChanged) {
            // Soft OR full. hasDecodedPixels is full-only; leaving prior soft
            // made canAccept reject the next path's LQIP (shown edge ≥ incoming).
            if (item->hasDisplayPixels()) {
                item->clearDecodedPixels();
            }
            m_host->clearLiveContentMeta(item);
            m_host->syncLiveColorFromState(item, ColorAdjustments{});
        } else if (item->hasDecodedPixels()) {
            // Same path soft→HQ: clear full so soft can attach.
            item->clearDecodedPixels();
        }
        // Soft underlay always materializes host-raw + ItemWorld want (ECS).
        // Unverified displayReady baked soft caused random Workspace→Image orient.
        // resolveImageModePendingPixels no longer returns filmstrip overrides as
        // displayReady; if still set, prefer host-raw from cache then materialize.
        const WorkspaceItemState want = wantAppearanceForItem(item, item->sessionId());
        if (displayReady && SessionAppearance::hasContentAppearance(want)) {
            QImage host = ImageCache::get(path);
            if (host.isNull()) {
                host = ThumtooCache::cachedLqipImage(path);
            }
            if (!host.isNull()) {
                installDisplayPixels(item, host, SessionAppearance::PixelKind::SoftPreview,
                                     item->sessionId());
            }
            // No host-raw: do NOT attach displayReady bake (ECS_GUI_BYPASSES #1).
            // Layout still follows want below; decode will fill pixels from host.
        } else if (displayReady) {
            // Identity want: filmstrip/host soft is safe to attach as underlay.
            attachDisplaySample(item, pixels, want,
                                SessionAppearance::PixelKind::SoftPreview);
        } else {
            installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }

        // Intrinsic only from definitive file size — never from soft/LQIP sz.
        const QSize known = m_host->logicalSizeForPath(path);
        QSize targetSize = item->imageSize();
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !m_host->hostSizeBook().isProvisional(path)) {
            targetSize = ContentXform::layoutSize(known, want);
        }
        int didFit = 0;
        if (isPositiveSize(targetSize) && targetSize.width() > 1
            && !m_host->hostSizeBook().isProvisional(path)) {
            hostSetIntrinsicSize(item, targetSize);
            const bool needFit =
                sizeBefore.width() <= 1
                || ContentXform::aspectChanged(sizeBefore, targetSize);
            if (needFit || m_host->hostFraming().isStickyZoomEnabled() || m_host->hostFraming().hasPreservedViewScale()) {
                // Aspect change, sticky mode, or free-zoom preserve across files.
                resetImageModeItemPlacement(item);
                m_host->applyImageModeFraming(item);
                didFit = 1;
            } else if (sizeBefore != targetSize) {
                m_host->preserveImageViewOnLogicalSizeChange(item, sizeBefore, targetSize);
            }
        }
        // Single press: sync repaint so soft is visible before PreferCache.
        // Key-repeat (nav hot): async update only — sync repaint every auto-repeat
        // event was the cumulative GUI freeze under held ←/→.
        if (m_host->viewportWidget()) {
            if (m_host->hostSlideshow().hud().isNavHot()) {
                m_host->viewportWidget()->update();
            } else {
                m_host->viewportWidget()->repaint();
            }
        }
        // Retained path RAM: bind session and paint tiles without waiting for
        // the next coordinator timer (A→B→A should show tiles on this frame).
        if (!m_host->hostSlideshow().hud().isNavHot() && item->tileLodHasPathRam()) {
            tickItemTileLod(item, 8);
        }
        biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d fit=%d painted=%s",
                      qPrintable(QFileInfo(path).fileName()),
                      pixels.width(), pixels.height(), didFit,
                      m_host->hostSlideshow().hud().isNavHot() ? "async" : "sync");
        return;
    }

    // No reusable item — still try to capture from whatever was on the canvas.
    if (!m_host->liveItems().isEmpty()) {
        m_host->captureStickyPanAnchor(m_host->liveItems().first());
    }
    m_host->clearLiveCanvas();
    item = createPlaceholderItem(path, isPositiveSize(sz) ? sz : QSize(1, 1));
    if (!item) {
        m_host->setUpdatesEnabled(true);
        return;
    }
    bindImageModeSessionCursor(item);
    installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                         m_host->hostSessionId().currentIdValue());
    resetImageModeItemPlacement(item);
    // Do not prepareImageModeCanvas() here — it zeros sceneRect after the item
    // exists (same class of empty ImageView as the cold-placeholder path).
    m_host->syncImageModeSceneRect(item);
    m_host->applyImageModeFraming(item);
    m_host->setUpdatesEnabled(true);
    if (m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
    m_host->notifyStatusChanged();
    biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height());
}

void DisplayPipelineController::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    // Suppress paints between removing the old item and fitting the new one
    // so we never present a native-scale (or empty) intermediate frame.
    m_host->setUpdatesEnabled(false);
    // Preserve sticky pan across the wipe (soft→full or cold replace).
    if (!m_host->liveItems().isEmpty()) {
        if (m_host->liveItems().first()->path() != path) {
            m_host->captureStickyPanAnchor(m_host->liveItems().first());
        } else if (m_host->hostFraming().isStickyZoomEnabled()
                   && !m_host->hostFraming().isStickyFit()) {
            // Same path rebuild: keep looking where we are now.
            m_host->captureStickyPanAnchor(m_host->liveItems().first());
        }
    }
    // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
    m_host->clearLiveCanvas();
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        m_host->setUpdatesEnabled(true);
        m_host->hostSessionId().setLastLoadError(path);
        m_host->notifyStatusChanged();
        return;
    }
    // Filmstrip overrides are not driven by decode (selection/nav).
    // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
    // Arbitrary Workspace rotation stays on the free-form item only.
    // m_view->createItemFromImage materializes host × store want (install invariant).
    bindImageModeSessionCursor(item);
    resetImageModeItemPlacement(item);
    // Content flips/crop are materialised in createItemFromImage from sparse/XDG.
    // Do not re-apply path-book placement hFlip (pre–Stage 4 dual residual).
    m_host->prepareImageModeCanvas();
    frameImageModeReplaceItem(item, path);
    m_host->setUpdatesEnabled(true);
    if (m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
    m_host->notifyStatusChanged();
}

void DisplayPipelineController::completeLoadReplace(const QString &path, const QImage &image, quint64 generation)
{
    if (generation != loadGate().generation()) {
        return; // superseded by a newer navigation / open
    }
    // Stale navigation: only the current classic path may install.
    // Empty multi-item canvas can still seed from m_view->classicPath.
    if (path != m_host->hostImage().classicPath()) {
        return;
    }
    if (image.isNull()) {
        if (ThumtooCache::isAvailable()) {
            // Full native miss: PreferCache display ladder so onLadderReady can
            // upgrade Image mode (soft→HQ). Skip when tiles own display.
            if (!tickTilesIfOwnDisplay(path)) {
                requestEscalateClimb(path, ThumtooCache::kBatchOverviewEdge);
            }
            m_host->hostSessionId().clearLastLoadError();
        } else {
            m_host->hostSessionId().setLastLoadError(path);
        }
        m_host->notifyStatusChanged();
        return;
    }
    if (m_host->isImageMode()) {
        (void)tryInstallImageModeSample(path, image);
        return;
    }
    seedEmptyWorkspaceFromReplace(path, image);
}

bool DisplayPipelineController::tryInstallGalleryUnderlay(ImageItem *item)
{
    ASSERT_GUI_THREAD();
    if (!item || !m_host->isGalleryMode()) {
        return false;
    }
    if (item->hasDisplayPixels()) {
        return true;
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        return false;
    }
    const ImageSizeBook &book = m_host->hostSizeBook();
    if (book.isFailed(path) || !book.hasDefinitive(path)) {
        return false;
    }
    QImage under = ImageCache::get(path);
    if (under.isNull()) {
        return false;
    }
    if (ImageCache::longEdge(under) > DisplayQuality::kEmbeddedUnderlayMaxEdge) {
        const int cap = DisplayQuality::kEmbeddedUnderlayMaxEdge;
        under = under.scaled(cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (under.isNull()
        || ImageCache::longEdge(under) > DisplayQuality::kEmbeddedUnderlayMaxEdge) {
        return false;
    }
    const SessionImageId sid = item->sessionId();
    const int before = item->displayPixelLongEdge();
    installDisplayPixels(item, under, SessionAppearance::PixelKind::SoftPreview, sid);
    if (item->displayPixelLongEdge() <= before) {
        hostSetPreviewImage(item, under);
    }
    return item->hasDisplayPixels();
}

ImageItem *DisplayPipelineController::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    // Defer-populate blocks bulk setWorkspacePaths create while the size gate
    // is still arming. Progressive ordered ensurePlaceholders must create while
    // the gate is active (defer stays true until complete).
    // Never apply defer in Image mode (empty underlay after Gallery visit).
    if (m_host->isGalleryMode() && m_host->hostGalleryDecodeBook().isDeferPopulate()
        && !m_host->hostGallerySizeResolve().active()) {
        return nullptr;
    }
    if (m_host->isGalleryMode()
        && (!isPositiveSize(intrinsicSize) || intrinsicSize.width() <= 1
            || intrinsicSize.height() <= 1)) {
        return nullptr;
    }
    auto *item = new ImageItem(path, intrinsicSize);
    m_host->applyItemModeFlags(item);
    m_host->canvasScene()->addItem(item);
    m_host->liveItems().append(item);
    registerItemDisplaySurface(item);
    return item;
}



void DisplayPipelineController::scheduleTileLodAfterInteraction(int delayMs)
{
    if (!tileLodZoomDebounce()) {
        tileLodZoomDebounce() = new QTimer(m_host->hostObject());
        tileLodZoomDebounce()->setSingleShot(true);
        QObject::connect(tileLodZoomDebounce(), &QTimer::timeout, m_host->hostObject(), [this]() {
            // Always issue visible tiles first (scroll/pan settle). Gallery
            // uses this path for Ctrl+wheel; Image/Workspace also need LOD
            // coverage after scrollbar storms — not only PreferCache climb.
            tickPrimaryTileLod(8);
            if (m_host->isImageMode()) {
                maybeClimbImageModePixelsForView();
            } else if (m_host->isWorkspaceMode()) {
                ensureWorkspaceQualityClimb();
            }
        });
    }
    tileLodZoomDebounce()->setInterval(ViewTransform::nonNegMs(delayMs));
    tileLodZoomDebounce()->start();
}


void DisplayPipelineController::dropItemTileLodSession(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Stage 2: do not ensure a bag just to drop it (destroy/purge paths).
    if (auto it = m_tileBags.find(item); it != m_tileBags.end() && it->second) {
        it->second->resetSession();
    }
}

tilelod::ItemBag &DisplayPipelineController::ensureTileBag(ImageItem *item)
{
    Q_ASSERT(item);
    auto it = m_tileBags.find(item);
    if (it != m_tileBags.end()) {
        return *it->second;
    }
    auto bag = std::make_unique<tilelod::ItemBag>();
    item->attachTileLodBag(bag.get());
    tilelod::ItemBag &ref = *bag;
    m_tileBags.emplace(item, std::move(bag));
    return ref;
}

void DisplayPipelineController::releaseTileBag(ImageItem *item)
{
    if (!item) {
        return;
    }
    item->detachTileLodBag();
    m_tileBags.erase(item);
}

void DisplayPipelineController::releaseAllTileBags()
{
    for (auto &entry : m_tileBags) {
        if (entry.first) {
            entry.first->detachTileLodBag();
        }
    }
    m_tileBags.clear();
}

tilelod::ItemBag *DisplayPipelineController::tileLodBag(ImageItem *item)
{
    if (!item) {
        return nullptr;
    }
    auto it = m_tileBags.find(item);
    if (it == m_tileBags.end()) {
        return nullptr;
    }
    return it->second.get();
}

const tilelod::ItemBag *DisplayPipelineController::tileLodBag(const ImageItem *item) const
{
    if (!item) {
        return nullptr;
    }
    // Key is non-const ImageItem*; identity lookup only.
    auto it = m_tileBags.find(const_cast<ImageItem *>(item));
    if (it == m_tileBags.end()) {
        return nullptr;
    }
    return it->second.get();
}

void DisplayPipelineController::setItemTileLodSuppressed(ImageItem *item, bool on)
{
    if (!item) {
        return;
    }
    // Stage 2: pipeline writes bag.suppressed; item is not a parallel authority.
    tilelod::ItemBag &bag = ensureTileBag(item);
    if (bag.suppressed == on) {
        return;
    }
    bag.suppressed = on;
    if (on && bag.controller) {
        // Drop private session so paint cannot draw stale cells over the
        // crop-draft full frame; shared path cache is left intact.
        bag.controller.reset();
        bag.lastUpdateGen = 0;
        bag.gradedCache.clear();
        bag.gradeSig = 0;
    }
}

void DisplayPipelineController::tickItemTileLod(ImageItem *item, int budget)
{
    if (!item) {
        return;
    }
    ensureTileBag(item);
    item->tickTileLod(budget);
}

void DisplayPipelineController::purgeTilePathRam(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    for (ImageItem *item : m_host->liveItems()) {
        if (item && item->path() == path) {
            dropItemTileLodSession(item);
        }
    }
    tilelod::TileLodRegistry::instance().invalidate(path);
    m_host->hostTileNeighborPrefetch().dropPath(path);
}


void DisplayPipelineController::dropAllTileLodSessions()
{
    auto dropList = [this](const QList<ImageItem *> &items) {
        for (ImageItem *item : items) {
            dropItemTileLodSession(item);
        }
    };
    dropList(m_host->liveItems());
    dropList(m_host->hostGallery().stashedItems());
    dropList(m_host->hostWorkspace().stashedItems());
    // Reset any bag still in the map (identity-matched items above already reset).
    for (auto &entry : m_tileBags) {
        if (entry.second) {
            entry.second->resetSession();
        }
    }
}


void DisplayPipelineController::tickPrimaryTileLod(int budget)
{
    ASSERT_GUI_THREAD();
    // Key-repeat: do not plan/issue tiles — soft underlay only until settle.
    if (m_host->hostSlideshow().hud().isNavHot()) {
        return;
    }
    if (!tileCoordinator()) {
        tileCoordinator() = std::make_unique<TileLoadCoordinator>(this);
    }
    tileCoordinator()->tick(budget);

    // Keep issuing until visible coverage is done.
    // Gallery: only on-screen cells. Off-screen items stay tileLodWanted but
    // are never issued by the coordinator — checking them kept this timer at
    // 16 ms forever (GUI spin + worker wake) after the decode-window fix.
    // Image/Workspace: few items; climb every tileLodWanted until covered.
    bool needMore = false;
    if (m_host->isGalleryMode()) {
        QRectF sceneVis;
        if (m_host->viewportWidget()) {
            sceneVis = m_host->mapViewportToScene();
        }
        if (!sceneVis.isNull() && m_host->canvasScene()) {
            const QList<QGraphicsItem *> hit = m_host->canvasScene()->items(
                sceneVis, Qt::IntersectsItemBoundingRect);
            for (QGraphicsItem *gi : hit) {
                auto *ii = qgraphicsitem_cast<ImageItem *>(gi);
                if (!ii || !ii->tileLodWanted()) {
                    continue;
                }
                if (!ii->tileLodViewportCovered()) {
                    needMore = true;
                    break;
                }
            }
        }
    } else {
        for (ImageItem *ii : m_host->liveItems()) {
            if (!ii || !ii->tileLodWanted()) {
                continue;
            }
            if (!ii->tileLodViewportCovered()) {
                needMore = true;
                break;
            }
        }
    }
    if (!needMore) {
        return;
    }
    if (!tileLodTimer()) {
        tileLodTimer() = new QTimer(m_host->hostObject());
        tileLodTimer()->setSingleShot(true);
        QObject::connect(tileLodTimer(), &QTimer::timeout, m_host->hostObject(), [this]() {
            // Image focus: higher budget so density climb is not starved.
            tickPrimaryTileLod(m_host->isGalleryMode() ? 48 : 32);
        });
    }
    if (!tileLodTimer()->isActive()) {
        tileLodTimer()->start(16);
    }
}





void DisplayPipelineController::attachDisplaySample(ImageItem *item, const QImage &display,
                                                    const WorkspaceItemState &want,
                                                    SessionAppearance::PixelKind kind)
{
    if (!item || display.isNull() || !m_host) {
        return;
    }
    const QString path = item->path();

    // Display samples are always display-ready (materializeDisplay or host-raw
    // identity). Install via Ready/Preview only — the legacy setSourceImage path
    // (geometry re-entry + updateDisplayedPixmap) was removed as dead code.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        item->setPreviewImage(display);
    } else {
        item->setSourceImageReady(display);
    }

    // Layout: ONE rule — ContentXform::layoutSize(fileNative, want) via view host.
    // Soft/full sample pixels never define intrinsic (SIZE.md / CONTENT_PIPELINE).
    applyContentLayoutSize(item, want);
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")
        || (want.hasCrop && item->imageSize().width() <= 1)) {
        const QSize isz = item->imageSize();
        if (want.hasCrop && (isz.width() <= 1 || isz.height() <= 1)) {
            qCritical("attachDisplaySample: crop want but intrinsic %dx%d (display %dx%d path=%s)",
                      isz.width(), isz.height(), display.width(), display.height(),
                      qPrintable(path));
        }
    }

    m_host->syncLiveContentMetaFromState(item, want);
    {
        ColorAdjustments grade = want.colorAdjust;
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId && grade.isIdentity()
            && m_host->itemWorld().hasColor(sid)) {
            grade = m_host->itemWorld().color(sid).grade;
        }
        m_host->syncLiveColorFromState(item, grade);
    }
}



void DisplayPipelineController::applyContentLayoutSize(ImageItem *item,
                                                       const WorkspaceItemState &wantIn)
{
    if (!item || !m_host) {
        return;
    }
    // Placement/color-only durable rows are not content orient (2205–2211).
    // layoutOrientAuthorityWant also keeps orient when *want* already specifies
    // turns/flips/crop — bakeItemRotate90 applies layout *before* setContentBake,
    // so hasContentOrient is still false on the first 90° and must not strip.
    WorkspaceItemState want = wantIn;
    {
        const SessionImageId sid = resolveItemSessionId(item);
        if (sid != kInvalidSessionImageId) {
            want = SessionAppearance::layoutOrientAuthorityWant(
                m_host->itemWorld().hasContentOrient(sid), want);
        }
    }
    // Intrinsic is always ContentXform layout of file-native size — never sample
    // pixel dimensions. Using displayImage().size() for crops shrank Workspace
    // tiles to soft resolution (and stretched wrong pixels on re-crop).
    //
    // Provisional sizes still get orient/crop layout: a 90° content turn must
    // transpose the box even before the durable probe lands (filmstrip→Workspace
    // drop was leaving unrotated intrinsic + oriented pixels → clipped tile).
    const QString path = item->path();
    QSize fileNative = m_host->logicalSizeForPath(path);
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1 || fileNative.height() <= 1) {
        // Gallery: never invent intrinsic from samples/crop before definitive size.
        if (m_host->isGalleryMode() && !m_host->hostSizeBook().hasDefinitive(path)
            && !m_host->hostSizeBook().isFailed(path)) {
            return;
        }
        // Fall back: crop rect in recorded source space, or orient-only current.
        if (want.hasCrop && !want.cropRect.isEmpty()) {
            const QSize basis = (want.cropSourceSize.isValid()
                                && want.cropSourceSize.width() > 1)
                                   ? want.cropSourceSize
                                   : item->imageSize();
            const QRect c = SessionAppearance::scaleCropRect(
                want.cropRect.normalized(), want.cropSourceSize, basis);
            if (c.width() > 1 && c.height() > 1) {
                hostSetIntrinsicSize(item, c.size());
            }
        } else if (ContentXform::swapsAspect(ContentXform::Value::fromState(want))) {
            // Orient-only with no usable native: transpose current box if odd turns.
            const QSize cur = item->imageSize();
            if (isPositiveSize(cur) && cur.width() > 1 && cur.height() > 1) {
                hostSetIntrinsicSize(item, QSize(cur.height(), cur.width()));
            }
        }
        return;
    }
    const QSize lay = ContentXform::layoutSize(fileNative, want);
    if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
        hostSetIntrinsicSize(item, lay);
    }
}


bool DisplayPipelineController::tryRematerializeFromHost(ImageItem *item,
                                                         const WorkspaceItemState &want)
{
    if (!item || !m_host) {
        return false;
    }
    if (m_host->hostCrop().isCropDraftLockedItem(item)) {
        return false;
    }
    const QString path = item->path();
    const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull()) {
        return false;
    }
    if (ContentXform::longEdge(host.size()) > ContentXform::kGuiMaterializeMaxEdge) {
        return false;
    }
    // Prefer SoftPreview when the live item is soft-only so setPreviewImage
    // accepts the sample (setPreviewImage ignores soft when full is present).
    // If full is already shown, rematerialize as FullSource.
    const auto kind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    const QImage display =
        SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        return false;
    }
    attachDisplaySample(item, display, want, kind);
    return true;
}

void DisplayPipelineController::rematerializeItemContent(ImageItem *item,
                                                         const WorkspaceItemState &want)
{
    if (!item || !m_host) {
        return;
    }
    // Crop draft owns the live sample — pure rematerialize must not soft↔full.
    if (m_host->hostCrop().isCropDraftLockedItem(item)) {
        return;
    }
    if (tryRematerializeFromHost(item, want)) {
        return;
    }
    const QString path = item->path();
    // Host must be unoriented. Never bake from preview/display — that double-applies
    // crop when the tile already shows a soft crop (Gallery after Image crop).
    QImage raw = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (raw.isNull() && item->hasDecodedPixels()
        && !m_host->itemHasAppliedContentXform(item)) {
        // FullSource without applied xform is still host-shaped (rare).
        raw = item->sourceImage();
    }
    const SessionImageId sid = resolveItemSessionId(item);
    if (raw.isNull()) {
        // Cold cache: optional disk soft so interactive bake/open is not blank.
        // scheduleAsync no-ops when the only host is ≤ GUI edge (soft stand-in).
        if (!path.isEmpty()) {
            QImage disk = ImageLoader::loadThumbnail(
                path, ThumtooCache::kGalleryLadderEdge);
            if (disk.isNull()) {
                disk = ImageLoader::loadThumbnail(path, 512);
            }
            if (!disk.isNull()) {
                ImageCache::put(path, disk);
                QImage soft = disk;
                if (ContentXform::longEdge(disk.size())
                    > ContentXform::kGuiMaterializeMaxEdge) {
                    soft = ImageCache::clampToMaxEdge(
                        disk, ContentXform::kGuiMaterializeMaxEdge);
                }
                const QImage display = SessionAppearance::materializeDisplay(
                    soft, want, SessionAppearance::PixelKind::SoftPreview);
                if (!display.isNull()) {
                    if (item->hasDecodedPixels()) {
                        hostClearDecodedPixels(item);
                    }
                    attachDisplaySample(item, display, want,
                                        SessionAppearance::PixelKind::SoftPreview);
                }
            }
            if (SessionAppearance::hasContentAppearance(want)) {
                scheduleAsyncHostRematerialize(path, sid, want);
            }
        }
        return;
    }
    const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
    int edge = ContentXform::longEdge(raw.size());
    QImage host = raw;
    SessionAppearance::PixelKind bakeKind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    bool scheduleFull = false;
    if (edge > maxGui) {
        // Soft stand-in now (crop/orient visible); full bake async.
        host = ImageCache::clampToMaxEdge(raw, maxGui);
        edge = ContentXform::longEdge(host.size());
        bakeKind = SessionAppearance::PixelKind::SoftPreview;
        scheduleFull = true;
    }
    if (edge <= 0 || edge > maxGui) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    const QImage display = SessionAppearance::materializeDisplay(host, want, bakeKind);
    if (display.isNull()) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    if (bakeKind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        hostClearDecodedPixels(item);
    }
    attachDisplaySample(item, display, want, bakeKind);
    if (scheduleFull) {
        scheduleAsyncHostRematerialize(path, sid, want);
    }
}





void DisplayPipelineController::clearStaleAppliedFingerprintIfNeeded(ImageItem *item)
{
    if (!item || !m_host) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId || !m_host->itemWorld().hasDurableAppearance(sid)) {
        return;
    }
    if (!m_host->itemHasAppliedContentXform(item)) {
        return;
    }
    const WorkspaceItemState st = m_host->sessionAppearanceValue(sid);
    if (!SessionAppearance::hasContentAppearance(st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(st);
    if (ContentXform::equal(m_host->itemAppliedContentXform(item), want)) {
        return;
    }
    // Stash may hold a stale applied fingerprint from Image-mode edits that
    // were committed to ItemWorld while this tile was off-canvas.
    m_host->clearLiveContentMeta(item);
}

void DisplayPipelineController::reinstallModePixelsAfterIdentityReset(
    ImageItem *item, SessionImageId sid)
{
    if (!item || !m_host) {
        return;
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        hostClearDecodedPixels(item);
        return;
    }
    // Gallery → soft ladder; Image/Workspace → full on-disk decode.
    // Always drop pixels first so SoftPreview is not ignored while FullSource remains.
    if (m_host->isGalleryMode()) {
        galleryDecodeResetPath(path);
        hostClearDecodedPixels(item);
        const int softEdge = ThumtooCache::kGalleryLadderEdge;
        QImage soft = ImageLoader::loadThumbnail(path, softEdge);
        if (!soft.isNull()) {
            installDisplayPixels(item, soft, SessionAppearance::PixelKind::SoftPreview, sid);
        }
        // else: decode window will refill after soft state reset
    } else {
        const QImage full = fullRasterForEdit(path);
        if (!full.isNull()) {
            installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        } else {
            hostClearDecodedPixels(item);
        }
    }
}

bool DisplayPipelineController::installInteractiveSoftPreview(
    ImageItem *item, const WorkspaceItemState &want)
{
    if (!item || !m_host) {
        return false;
    }
    const QString path = item->path();
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull() && item->hasDecodedPixels()
        && !m_host->itemHasAppliedContentXform(item)) {
        host = item->sourceImage();
    }
    if (host.isNull()) {
        return false;
    }
    // materializeDisplay asserts NOT GUI when long edge > kGuiMaterializeMaxEdge.
    const int kInteractiveGradeMaxEdge = ContentXform::kGuiMaterializeMaxEdge;
    if (ImageCache::longEdge(host) > kInteractiveGradeMaxEdge) {
        host = ImageCache::clampToMaxEdge(host, kInteractiveGradeMaxEdge);
    }
    const auto kind = SessionAppearance::PixelKind::SoftPreview;
    const QImage display = SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        return false;
    }
    if (item->hasDecodedPixels()
        && ImageCache::longEdge(item->sourceImage()) > kInteractiveGradeMaxEdge) {
        // Soft stand-in for the drag; keep session id / path on the item.
        hostClearDecodedPixels(item);
    }
    attachDisplaySample(item, display, want, kind);
    return true;
}

void DisplayPipelineController::rematerializeGalleryItemFromStore(ImageItem *item)
{
    if (!item || !m_host) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    if (!m_host->itemWorld().hasDurableAppearance(sid)) {
        return;
    }
    const WorkspaceItemState st = m_host->sessionAppearanceValue(sid);
    if (!SessionAppearance::hasContentAppearance(st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(st);
    const ContentXform::Value applied = m_host->itemAppliedContentXform(item);
    if (m_host->itemHasAppliedContentXform(item) && ContentXform::equal(applied, want)
        && item->hasDisplayPixels()) {
        // Applied matches store; still fix layout if intrinsic is full-frame.
        applyContentLayoutSize(item, st);
        return;
    }
    clearStaleAppliedFingerprintIfNeeded(item);
    rematerializeItemContent(item, st);
}

void DisplayPipelineController::scheduleAsyncHostRematerialize(
    const QString &path, SessionImageId sid, const WorkspaceItemState &want)
{
    if (!m_host || path.isEmpty()) {
        return;
    }
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }
    const QImage hostProbe = ImageCache::get(path);
    if (hostProbe.isNull()) {
        return;
    }
    if (ContentXform::longEdge(hostProbe.size())
        <= ContentXform::kGuiMaterializeMaxEdge) {
        return; // GUI path already handled by tryRematerializeFromHost
    }
    const quint64 gen = loadGate().generation();
    QPointer<QObject> life(m_host->hostObject());
    DisplayPipelineController *pipe = this;
    const WorkspaceItemState wantCopy = want;
    QThreadPool::globalInstance()->start([life, pipe, path, sid, wantCopy, gen]() {
        if (!life || !pipe) {
            return;
        }
        const QImage host = ImageCache::get(path);
        if (host.isNull()) {
            return;
        }
        // Worker thread: multi-MP materialize is allowed.
        const QImage display = SessionAppearance::materializeDisplay(
            host, wantCopy, SessionAppearance::PixelKind::FullSource);
        if (display.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(life.data(), [life, pipe, path, sid, wantCopy, display, gen]() {
            if (!life || !pipe || !pipe->loadGate().accepts(gen)) {
                return;
            }
            pipe->finishAsyncHostRematerialize(path, sid, wantCopy, display);
        }, Qt::QueuedConnection);
    });
}

void DisplayPipelineController::finishAsyncHostRematerialize(
    const QString &path, SessionImageId sid, const WorkspaceItemState &want,
    const QImage &display)
{
    ASSERT_GUI_THREAD();
    if (!m_host || display.isNull() || path.isEmpty()) {
        return;
    }
    // Crop draft owns the target item — do not reinstall over orient-only draft.
    if (m_host->hostCrop().isCropDraftLockedPath(path)) {
        return;
    }
    ImageItem *item = nullptr;
    for (ImageItem *it : m_host->liveItems()) {
        if (!it || it->path() != path) {
            continue;
        }
        if (sid != kInvalidSessionImageId && it->sessionId() != sid
            && it->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        item = it;
        break;
    }
    if (!item) {
        return;
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);
    // Discard stale worker result if the store moved on for this session id.
    if (sid != kInvalidSessionImageId && m_host->itemWorld().hasDurableAppearance(sid)) {
        const WorkspaceItemState cur = m_host->sessionAppearanceValue(sid);
        if (!ContentXform::equal(ContentXform::Value::fromState(cur), wantX)) {
            return;
        }
    }
    // Already settled FullSource for this want at ≥ this resolution — skip.
    if (item->hasDecodedPixels() && m_host->itemHasAppliedContentXform(item)
        && ContentXform::equal(m_host->itemAppliedContentXform(item), wantX)
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(display))) {
        return;
    }
    const QSize before = item->imageSize();
    attachDisplaySample(item, display, want, SessionAppearance::PixelKind::FullSource);
    if (before != item->imageSize()) {
        m_host->preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
    }
    if (m_host->isImageMode() && m_host->canvasScene() && m_host->liveItems().size() == 1) {
        m_host->canvasScene()->setSceneRect(
            item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }
    if (m_host->isGalleryMode() && before != item->imageSize()) {
        m_host->requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
    if (m_host->isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    } else if (m_host->isImageMode()) {
        driveImageFocusSurface();
    }
    if (m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
}

void DisplayPipelineController::hostClearDecodedPixels(ImageItem *item)
{
    if (item) {
        item->clearDecodedPixels();
    }
}

void DisplayPipelineController::hostSetIntrinsicSize(ImageItem *item, const QSize &size)
{
    if (!item || !m_host) {
        return;
    }
    // Gallery: LQIP-scale boxes must not replace an already correct layout cell.
    // Do NOT compare against file-native area — cropped layoutSize is often much
    // smaller than native and must still apply.
    if (m_host->isGalleryMode() && isPositiveSize(size)) {
        const int newEdge = qMax(size.width(), size.height());
        if (newEdge > 0 && newEdge <= DisplayQuality::kLqipMaxEdge) {
            const QSize cur = item->imageSize();
            if (isPositiveSize(cur)
                && qMax(cur.width(), cur.height()) > DisplayQuality::kLqipMaxEdge * 2) {
                return;
            }
        }
    }
    item->setIntrinsicSize(size);
}

void DisplayPipelineController::hostSetPreviewImage(ImageItem *item, const QImage &preview)
{
    if (item) {
        item->setPreviewImage(preview);
    }
}

void DisplayPipelineController::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    if (!item || !m_host || quarterTurns == 0) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = m_host->captureContentBakeBeforeState(item);

    const SessionImageId sid = resolveItemSessionId(item);
    const int turns = ContentXform::normalizeQuarterTurns(
        beforeSt.contentQuarterTurns + quarterTurns);
    WorkspaceItemState cropMap = m_host->appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentRotate90(cropMap, quarterTurns);

    WorkspaceItemState want = beforeSt;
    want.contentQuarterTurns = turns;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    m_host->syncLiveContentMetaFromState(item, want);

    rematerializeItemContent(item, want);
    applyContentLayoutSize(item, want);

    {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        s.path = item->path();
        s.contentQuarterTurns = turns;
        if (sid != kInvalidSessionImageId) {
            m_host->itemWorld().setContentBake(sid, ItemComponents::contentBakeFromState(s));
            m_host->itemWorld().setCrop(sid, ItemComponents::cropFromState(s));
            m_host->persistDurableContentAppearance(item, s, "bakeRotate");
        }
        if (sid == kInvalidSessionImageId) {
            WorkspaceItemState pathSlot;
            if (const WorkspaceItemState *st = m_host->itemWorld().getPathState(item->path())) {
                pathSlot = *st;
            }
            pathSlot.path = item->path();
            pathSlot.contentQuarterTurns = turns;
            pathSlot.contentHFlip = want.contentHFlip;
            pathSlot.contentVFlip = want.contentVFlip;
            pathSlot.hasCrop = want.hasCrop;
            pathSlot.cropRect = want.cropRect;
            pathSlot.cropRotation = want.cropRotation;
            pathSlot.cropSourceSize = want.cropSourceSize;
            m_host->itemWorld().setPathState(item->path(), pathSlot);
        }
    }

    m_host->commitItemSessionEdit(item);

    if (m_host->isImageMode() && m_host->canvasScene() && m_host->liveItems().size() == 1) {
        m_host->canvasScene()->setSceneRect(
            item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    WorkspaceItemState afterSt = want;
    ItemComponents::applyPlacementToState(afterSt, item->placement());
    afterSt.sessionId = beforeSt.sessionId;
    m_host->pushItemContentCommand(m_host->hostTr("Rotate"), item, beforeSrc,
                                   item->sourceImage().copy(), beforeSt, afterSt);
}

void DisplayPipelineController::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    if (!item || !m_host || (!horizontal && !vertical)) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = m_host->captureContentBakeBeforeState(item);

    bool h = beforeSt.contentHFlip;
    bool v = beforeSt.contentVFlip;
    int turns = beforeSt.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    const bool swapAxes = (turns == 1 || turns == 3);
    const bool srcH = swapAxes ? vertical : horizontal;
    const bool srcV = swapAxes ? horizontal : vertical;
    if (srcH) {
        h = !h;
    }
    if (srcV) {
        v = !v;
    }

    const SessionImageId sid = resolveItemSessionId(item);
    WorkspaceItemState cropMap = m_host->appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentFlip(cropMap, horizontal, vertical);

    WorkspaceItemState want = beforeSt;
    want.contentHFlip = h;
    want.contentVFlip = v;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentQuarterTurns = cropMap.contentQuarterTurns;

    m_host->syncLiveContentMetaFromState(item, want);
    rematerializeItemContent(item, want);
    applyContentLayoutSize(item, want);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        m_host->itemWorld().setContentBake(sid, ItemComponents::contentBakeFromState(s));
        m_host->itemWorld().setCrop(sid, ItemComponents::cropFromState(s));
        m_host->persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = want;
        ItemComponents::applyPlacementToState(s, item->placement());
        m_host->itemWorld().setPathState(item->path(), s);
    }

    m_host->commitItemSessionEdit(item);

    WorkspaceItemState afterSt = want;
    ItemComponents::applyPlacementToState(afterSt, item->placement());
    afterSt.sessionId = beforeSt.sessionId;
    const QString text = horizontal && !vertical ? m_host->hostTr("Flip horizontal")
        : vertical && !horizontal ? m_host->hostTr("Flip vertical")
        : m_host->hostTr("Flip");
    m_host->pushItemContentCommand(text, item, beforeSrc, item->sourceImage().copy(),
                                   beforeSt, afterSt);
}


QImage DisplayPipelineController::blockingExportDisplayForItem(const ImageItem *item) const
{
    if (!item || item->path().isEmpty()) {
        return {};
    }
    const QString path = item->path();
    const WorkspaceItemState want = wantAppearanceForItem(item, item->sessionId());
    const QImage fallback = item->displayImage();

    struct Shared {
        QMutex mu;
        QWaitCondition cv;
        QImage out;
        bool done = false;
    };
    auto shared = std::make_shared<Shared>();
    QThreadPool::globalInstance()->start([path, want, shared]() {
        ASSERT_NOT_GUI_THREAD();
        QImage host = ImageCache::get(path);
        const int hostEdge = ImageCache::longEdge(host);
        bool needLoad = host.isNull();
        if (!needLoad) {
            const QSize cached = ThumtooCache::cachedSize(path);
            if (cached.isValid() && cached.width() > 0 && cached.height() > 0) {
                const int native = ContentXform::longEdge(cached);
                if (hostEdge < native) {
                    needLoad = true;
                }
            } else if (hostEdge > 0 && hostEdge <= ThumtooCache::kBatchOverviewEdge) {
                needLoad = true;
            }
        }
        if (needLoad) {
            const QImage loaded = ImageLoader::load(path);
            if (!loaded.isNull()) {
                host = loaded;
                ImageCache::put(path, loaded);
            }
        }
        QImage display;
        if (host.isNull()) {
            display = {};
        } else if (!SessionAppearance::hasContentAppearance(want)
                   && want.colorAdjust.isIdentity()) {
            display = host;
        } else {
            display = SessionAppearance::materializeDisplay(
                host, want, SessionAppearance::PixelKind::FullSource);
        }
        QMutexLocker lock(&shared->mu);
        shared->out = display;
        shared->done = true;
        shared->cv.wakeOne();
    });
    QMutexLocker lock(&shared->mu);
    while (!shared->done) {
        shared->cv.wait(&shared->mu);
    }
    return shared->out.isNull() ? fallback : shared->out;
}

void DisplayPipelineController::applyProbedImageSize(const QString &path, const QSize &size)
{
    GUI_BUDGET("DisplayPipelineController::applyProbedImageSize");
    if (!m_host || path.isEmpty() || !size.isValid()) {
        return;
    }
    bool any = false;
    for (ImageItem *item : m_host->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        // Probe is authoritative file-native size. Layout = ContentXform
        // (turns + crop), not a simple axis swap.
        const SessionImageId sid = resolveItemSessionId(item);
        WorkspaceItemState want = wantAppearanceForItem(item, sid);
        const QSize layoutSize = SessionAppearance::layoutSizeOrNative(size, want);
        const QSize cur = item->imageSize();
        if (cur == layoutSize) {
            continue;
        }
        hostSetIntrinsicSize(item, layoutSize);
        any = true;
        // Drop stale pack clip: square (or wrong-aspect) galleryCellSize was
        // cropping the updated contentRect until the next pack.
        if (m_host->isGalleryMode()
            && SessionAppearance::galleryCellAspectStale(item->galleryCellSize(),
                                                         layoutSize)) {
            item->setGalleryCellSize({});
        }
        if (m_host->isImageMode() && item == m_host->targetItem()) {
            m_host->preserveImageViewOnLogicalSizeChange(item, cur, layoutSize);
        }
    }
    if (any && m_host->isGalleryMode() && !m_host->hostLayout().isFreeForm()) {
        // ContentChange is allowed during the size gate (prefix pack). Debounced
        // so sizeReady chunks do not reflow every path.
        m_host->requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    } else if (any && m_host->viewportWidget()) {
        m_host->viewportWidget()->update();
    }
    // Slideshow paints from path→logical, not the underlay item. When the probe
    // lands for a phase path, refresh dest aspect (and atlas if needed).
    SlideshowController &ss = m_host->hostSlideshow();
    if (ss.hud().isProgressActive() && ss.phase().isPhasePath(path)) {
        if (ss.phase().isFromPath(path) && ss.phase().hasFromImage()) {
            ss.requestDwellAtlasRebuild();
        }
        if (ss.phase().isToPath(path) && ss.phase().hasToImage()) {
            ss.requestToPhaseAtlasRebuild();
        }
        if (m_host->viewportWidget()) {
            m_host->viewportWidget()->update();
        }
    }
}
