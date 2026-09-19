// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipelinecontroller.h"

#include "tile_load_coordinator.h"

#include "imageview.h"
#include "imageitem.h"
#include "displayedgepolicy.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"
#include "pagepath.h"
#include "gallerysoftsm.h"
#include "displayquality.h"
#include "softdisplaypolicy.h"
#include "biltoo_thread.h"

#include "imageloader.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "coloradjust.h"
#include "imagesizebook.h"
#include "viewtransform.h"
#include "biltoo_logging.h"
#include "ttfp_trace.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>
#include <cstdlib>
#include <cstdio>

#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QVarLengthArray>
#include <QTimer>

namespace {

/** Queue onImagePreviewLoaded on the GUI thread; no-op if @a guard is gone. */
void queuePreviewLoaded(const QPointer<ImageView> &guard, const QString &path,
                        const QImage &preview, quint64 gen, int role)
{
    if (!guard || preview.isNull()) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, preview, gen, role]() {
        if (!guard) {
            return;
        }
        guard->onImagePreviewLoaded(path, preview, gen, role);
    });
}

/** Queue onImageLoaded on the GUI thread; no-op if @a guard is gone. */
void queueImageLoaded(const QPointer<ImageView> &guard, const QString &path,
                      const QImage &image, quint64 gen, int role)
{
    if (!guard) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, image, gen, role]() {
        if (!guard) {
            return;
        }
        guard->onImageLoaded(path, image, gen, role);
    });
}

/** Worker LQIP/soft underlay — see SoftDisplayPolicy::lqipOrCachedSoft. */
QImage loadSoftPreviewPixels(const QString &path, int /*softEdge*/)
{
    return SoftDisplayPolicy::lqipOrCachedSoft(path);
}

/**
 * LQIP seed job only. Never SoftOnly / loadThumbnail / PreferCache soft encode.
 * Gallery product is tiles + LQIP underlay only.
 */
void startSoftPreviewJob(const QPointer<ImageView> &guard, const QString &path,
                         quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp)
{
    biltooLoadDbg("lqipSeed START path=%s gen=%llu",
                  qPrintable(QFileInfo(path).fileName()),
                  static_cast<unsigned long long>(gen));
    Q_UNUSED(softEdge);
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, sessionApp]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            ThumtooCache::scheduleProbe(path);
            QImage preview = loadSoftPreviewPixels(path, 0);
            if (!preview.isNull() && !path.isEmpty()) {
                ImageCache::put(path, preview);
            }
            Q_UNUSED(sessionApp);
            queuePreviewLoaded(guard, path, preview, gen, roleInt);
        },
        2);
}

/**
 * Low-priority pool job: PreferCache / loadThumbnail at a display edge
 * (slideshow quality climb). Schedules PreferCache on miss.
 */
void startDisplayQualityJob(const QPointer<ImageView> &guard, const QString &path,
                            quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp)
{
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, qualityEdge, sessionApp]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            // Keep any host sample; do not require qualityEdge up front or a
            // smaller soft is discarded and the UI waits on high-res only.
            QImage image = ImageCache::get(path, qualityEdge);
            if (image.isNull()) {
                image = ImageCache::get(path);
            }
            if (!ImageCache::adequate(image, qualityEdge)) {
                // Tiles/TileSynth only when thumtoo is up — never soft PreferCache
                // encode. Without thumtoo, classic shrink decode as last resort.
                if (ThumtooCache::isAvailable()) {
                    const int edge = DisplayEdgePolicy::tileSynthEdge(
                        qualityEdge, ThumtooCache::kBatchOverviewEdge);
                    (void)ThumtooCache::scheduleTileSynthOrPyramid(path, edge);
                } else {
                    const QImage loaded =
                        ImageLoader::loadThumbnail(path, qualityEdge);
                    if (!loaded.isNull()
                        && ImageCache::longEdge(loaded)
                            >= ImageCache::longEdge(image)) {
                        image = loaded;
                    }
                }
            }
            if (!guard) {
                return;
            }
            if (image.isNull()) {
                // PreferCache/Full only via PathRaster on the GUI (contract §1).
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }
            // Soft stand-in still upgrades the view; climb via PathRaster on GUI.
            if (!ImageCache::adequate(image, qualityEdge) && ThumtooCache::isAvailable()) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
            }
            // Host-raw only; GUI install materializes (soft stand-in + async
            // for multi-MP). Do not bake here — same double-bake/cache pollution
            // as the soft job.
            if (!image.isNull() && !path.isEmpty()) {
                ImageCache::put(path, image);
            }
            if (ImageCache::longEdge(image) > qualityEdge) {
                image = image.scaled(qualityEdge, qualityEdge, Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
            }
            Q_UNUSED(sessionApp);
            queueImageLoaded(guard, path, image, gen, roleInt);
        },
        -1);
}


} // namespace

DisplayPipelineController::DisplayPipelineController(ImageView *view)
    : m_view(view)
{
}

void DisplayPipelineController::ensureWorkspaceQualityClimb()
{
    ASSERT_GUI_THREAD();
    if (!m_view->isWorkspaceMode() || !m_view->hostPathRaster() || !m_view->canvasScene()) {
        return;
    }
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : m_view->canvasScene()->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            targets.append(ii);
        }
    }
    if (targets.isEmpty()) {
        // No selection: climb all on-canvas items (bounded).
        int n = 0;
        for (ImageItem *ii : m_view->liveItems()) {
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
    m_view->tickPrimaryTileLod(8);

    for (ImageItem *ii : targets) {
        if (!ii) {
            continue;
        }
        const QString path = ii->path();
        if (path.isEmpty()) {
            continue;
        }
        if (m_view->isCropDraftLockedPath(path)) {
            continue;
        }
        // Deep zoom: tiles own display; skip PreferCache whole-frame climb.
        if (DisplayEdgePolicy::tilesOwnDisplay(ii->tileLodWanted(), false)) {
            continue;
        }
        // Always measure need after the current view transform (zoom/pan).
        const int needEdge = m_view->itemOnScreenNeedEdge(ii, /*allowHighRes=*/true);
        const int have = ii->displayPixelLongEdge();
        if (needEdge > 0 && have > 0 && DisplayEdgePolicy::coversEdge(have, needEdge)) {
            continue;
        }
        const bool pending = m_view->hostPathRaster()->isClimbPending(path);
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
                      m_view->displaySurfaceStateForItem(ii, -1, pending));
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
            (void)m_view->applyDisplaySurfaceAction(ii, act, QImage(), needEdge, pol);
        }
        if (!DisplayEdgePolicy::coversEdge(ii->displayPixelLongEdge(), needEdge)
            && (m_view->hostPathRaster()->isGaveUp(path)
                || (!m_view->hostPathRaster()->isClimbPending(path)
                    && act.type == DisplaySurface::ActionType::ScheduleClimb))) {
            // PreferCache plateau short of need — native only when no durable tiles.
            if (!ThumtooCache::hasDurableTilesKnown(path)
                && (m_view->hostPathRaster()->isGaveUp(path)
                    || !m_view->hostPathRaster()->isClimbPending(path))) {
                m_view->scheduleImageModeNativeDecodeOnce(path);
            }
        }
    }
}

void DisplayPipelineController::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty() || m_view->hostGallerySoftBook().hasImageModeNativeDecode(path)) {
        return;
    }
    m_view->hostGallerySoftBook().markImageModeNativeDecode(path);
    const quint64 gen = loadGate().generation();
    const QPointer<ImageView> guard(m_view);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        ASSERT_NOT_GUI_THREAD();
        const QImage decoded = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, decoded, gen]() {
                ImageView *const host = guard.data();
                if (!host) {
                    return;
                }
                if (!decoded.isNull()) {
                    ImageCache::put(path, decoded);
                }
                if (host->isImageMode()) {
                    if (gen != host->m_displayPipeline.loadGate().generation()) {
                        return;
                    }
                    if (!decoded.isNull()) {
                        (void)host->tryInstallImageModeSample(path, decoded);
                    }
                } else if (host->isWorkspaceMode() && !decoded.isNull()) {
                    host->onImagePreviewLoaded(
                        path, decoded, host->m_displayPipeline.loadGate().generation(),
                        static_cast<int>(ImageView::LoadAdd));
                }
            },
            Qt::QueuedConnection);
    });
}
void DisplayPipelineController::scheduleImageModePreferCacheClimb(const QString &path, int wantEdge)
{
    requestEscalateClimb(path, wantEdge);
}
void DisplayPipelineController::requestEscalateClimb(const QString &path, int wantEdge)
{
    if (!m_view->hostPathRaster() || path.isEmpty() || m_view->hostSlideshow().hud().isNavHot()) {
        return;
    }
    if (m_view->isCropDraftLockedPath(path)) {
        return;
    }
    // Tiles own display: tileLodWanted or known durable pyramid — no PreferCache.
    if (ImageItem *it = imageModeItemForPath(path)) {
        if (it->tileLodWanted() || ThumtooCache::hasDurableTilesKnown(path)) {
            m_view->tickPrimaryTileLod(12);
            return;
        }
    }
    for (ImageItem *ii : m_view->liveItems()) {
        if (ii && ii->path() == path
            && (ii->tileLodWanted() || ThumtooCache::hasDurableTilesKnown(path))) {
            m_view->tickPrimaryTileLod(12);
            return;
        }
    }
    if (ThumtooCache::hasDurableTilesKnown(path)) {
        m_view->tickPrimaryTileLod(12);
        return;
    }
    const int edge = m_view->cappedDisplayEdgeForPath(
        path, wantEdge > 0 ? wantEdge : ThumtooCache::kImageLadderEdge);
    // Slideshow + durable tiles: SoftDisplay (PreferCache/TileSynth) only —
    // EscalateToFull native decode is the CPU storm on prepared libraries.
    const auto policy =
        (m_view->hostSlideshow().hud().isProgressActive() && ThumtooCache::hasDurableTilesKnown(path))
            ? PathRasterService::ClimbPolicy::SoftDisplay
            : PathRasterService::ClimbPolicy::EscalateToFull;
    biltooLoadDbg("escalateClimb(service) path=%s edge=%d policy=%d",
                  qPrintable(QFileInfo(path).fileName()), edge,
                  static_cast<int>(policy));
    m_view->hostPathRaster()->ensure(path, edge, m_view->logicalSizeForPath(path), policy);
}
void DisplayPipelineController::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    if (m_view->hostPathRaster() && !path.isEmpty()) {
        m_view->hostPathRaster()->noteDelivery(path, requestEdge, sample);
    }
}
void DisplayPipelineController::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    if (path.isEmpty() || m_view->hostSlideshow().hud().isNavHot() || !m_view->hostPathRaster()) {
        return;
    }
    if (m_view->isCropDraftLockedPath(path)) {
        return;
    }
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    // Tiles own display once wanted or durable pyramid is known — no PreferCache.
    const bool durable = ThumtooCache::hasDurableTilesKnown(path);
    if (ImageItem *it = imageModeItemForPath(path)) {
        if (DisplayEdgePolicy::tilesOwnDisplay(it->tileLodWanted(), durable)) {
            m_view->tickPrimaryTileLod(12);
            return;
        }
    }
    if (durable) {
        m_view->tickPrimaryTileLod(12);
        return;
    }
    if (!sample.isNull() && m_view->sampleCoversNativeLogical(path, sample)) {
        return;
    }

    const int need = m_view->imageModeOnScreenNeedEdge();
    const int have = sample.isNull() ? 0 : ImageCache::longEdge(sample);
    // Cold path only (no durable tiles): climb to on-screen need, not soft-512 habit.
    const int escalated = DisplayEdgePolicy::escalateClimbTo(
        ThumtooCache::kBatchOverviewEdge, need);
    const int climbTo = DisplayEdgePolicy::climbEdgeIfNeeded(
        have, need, ThumtooCache::kBatchOverviewEdge,
        m_view->cappedDisplayEdgeForPath(path, escalated));
    if (climbTo <= 0) {
        return;
    }
    biltooLoadDbg("imageModeClimb(service) path=%s climbTo=%d have=%d need=%d cold",
                  qPrintable(QFileInfo(path).fileName()), climbTo, have, need);
    m_view->hostPathRaster()->ensure(path, climbTo, m_view->logicalSizeForPath(path),
                         PathRasterService::ClimbPolicy::EscalateToFull);
    // Full is async. If terminal or nothing pending and still short, host native
    // (only when no durable tiles — prepared libs use TileSynth / tile LOD).
    if (!ThumtooCache::hasDurableTilesKnown(path)
        && !DisplayEdgePolicy::coversEdge(m_view->hostPathRaster()->haveEdge(path), climbTo)
        && (m_view->hostPathRaster()->isGaveUp(path) || !m_view->hostPathRaster()->isClimbPending(path))) {
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
    // Same rules as every other attach: accept → materialize → m_view->attachDisplaySample.
    m_view->installDisplayPixels(item, image, kind, item->sessionId() != kInvalidSessionImageId
                                             ? item->sessionId()
                                             : m_view->hostSessionId().currentIdValue());
    m_view->hostSessionId().clearLastLoadError();
    m_view->rememberSizeFromDecode(path, image);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
}


int DisplayPipelineController::cappedDisplayEdgeForPath(const QString &path, int wantEdge) const
{
    int nativeLong = 0;
    const QSize logical = m_view->logicalSizeForPath(path);
    if (isPositiveSize(logical) && !m_view->isProvisionalImageSize(path)) {
        nativeLong = ContentXform::longEdge(logical);
    }
    return DisplayEdgePolicy::cappedDisplayEdge(wantEdge, nativeLong);
}

bool DisplayPipelineController::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    const int incoming = ImageCache::longEdge(image);
    int nativeLong = 0;
    bool nativeKnown = false;
    const QSize logical = m_view->logicalSizeForPath(path);
    if (isPositiveSize(logical) && !m_view->isProvisionalImageSize(path)) {
        nativeLong = ContentXform::longEdge(logical);
        nativeKnown = nativeLong > 0;
    }
    return DisplayEdgePolicy::sampleCoversNative(
        incoming, nativeLong, nativeKnown, ThumtooCache::kBatchOverviewEdge,
        ThumtooCache::kImageLadderEdge);
}

bool DisplayPipelineController::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    if (!m_view->isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    // @p image is host-raw (soft job, ladder, quality job). Single materialize
    // in installDisplayPixels — soft stand-in + async full when multi-MP want.
    const SessionAppearance::PixelKind kind = m_view->pixelKindForImageModeSample(path, image);
    const bool ok = tryInstallImageModeSampleBaked(path, image, kind);
    // Decide soft→async / climb from the new host edge (event-driven).
    // Nav-hot: install only — driveImageFocusSurface is a no-op while hot.
    m_view->driveImageFocusSurface();
    if (ok && m_view->viewport()) {
        m_view->viewport()->update();
    }
    return ok;
}

bool DisplayPipelineController::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    // Name is historical: @p image is host-raw. installDisplayPixels materializes.
    if (!m_view->isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    if (m_view->isCropDraftLockedPath(path)) {
        return false;
    }
    if (ImageItem *cur = imageModeItemForPath(path)) {
        if (m_view->canAcceptDisplaySample(cur, image, kind)) {
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
            if (!(noUpgrade && m_view->hostPathRaster() && m_view->hostPathRaster()->isGaveUp(path))) {
                ensureImageModeQualityClimb(path, image);
            } else {
                // Terminal PreferCache/Full shortfall at same edge as painted —
                // still need host native when on-screen need exceeds that.
                const int need = m_view->imageModeOnScreenNeedEdge();
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
        m_view->installImageModePendingTile(path, image);
        ensureImageModeQualityClimb(path, image);
        emit m_view->statusChanged();
    } else {
        m_view->installImageModeReplaceItem(path, image);
        if (!sampleCoversNativeLogical(path, image)) {
            ensureImageModeQualityClimb(path, image);
        }
    }
    return true;
}

void DisplayPipelineController::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty()) {
        return;
    }
    // Central climb policy + ImageCache put. Slideshow installs via
    // PathRasterService::rasterImproved (no second direct call).
    if (m_view->hostPathRaster()) {
        m_view->hostPathRaster()->noteDelivery(path, maxEdge, image);
        // PreferCache BestAvailable / Full escalate is PathRasterService policy
        // (docs/THUMTOO_HOST_CONTRACT.md). Do not recover ad-hoc here.
    } else {
        if (!image.isNull()) {
            ImageCache::put(path, image);
        }
        if (m_view->hostSlideshow().hud().isProgressActive() && !image.isNull()) {
            m_view->hostSlideshow().onSlideshowRasterReady(path, image);
        }
    }

    // Image mode: soft→sharp when full ImageLoader::load missed or is still
    // in flight. ladderReady used to return early for non-Gallery, so PDF /
    // page / archive PreferCache deliveries left the view stuck on the soft
    // thumbnail forever.
    if (m_view->isImageMode() && !image.isNull() && !m_view->hostSlideshow().hud().isProgressActive()) {
        upgradeImageModeFromLadder(path, maxEdge, image);
    }

    // Workspace: was falling through to early return without installing samples.
    if (m_view->isWorkspaceMode() && !image.isNull()) {
        applyWorkspaceLadderReady(path, maxEdge, image);
    }

    // Crop may be open in Image or Workspace on a provisional sample.
    if (!image.isNull()) {
        m_view->maybeUpgradeCropFullRaster(path, image);
    }

    // PreferCache/FocusFull may have co-built durable tiles; wake tile LOD only
    // if some on-canvas item for this path already wants tiles (avoids a full
    // tickPrimary scan on every soft delivery).
    if (m_view->isImageMode() || m_view->isWorkspaceMode() || m_view->isGalleryMode()) {
        for (ImageItem *ii : m_view->liveItems()) {
            if (ii && ii->path() == path && ii->tileLodWanted()) {
                m_view->tickPrimaryTileLod(12);
                break;
            }
        }
    }

    if (!m_view->isGalleryMode()) {
        return;
    }
    applyGalleryLadderReady(path, maxEdge, image);
}


void DisplayPipelineController::upgradeImageModeFromLadder(const QString &path, int maxEdge,
                                           const QImage &image)
{
    // ladderReady Image-mode path: same install policy as completeLoadReplace.
    if (path.isEmpty() || image.isNull() || path != m_view->classicPath()) {
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
    if (!m_view->isGalleryMode() || path.isEmpty()) {
        return;
    }
    Q_UNUSED(maxEdge);
    // Gallery accepts LQIP only. Larger soft samples stay in ImageCache for
    // filmstrip / Image mode — not painted onto Gallery cells.
    if (!image.isNull()
        && ImageCache::longEdge(image) <= DisplayQuality::kLqipMaxEdge) {
        ImageCache::put(path, image);
        for (ImageItem *item : m_view->liveItems()) {
            if (!item || item->path() != path || item->hasDisplayPixels()) {
                continue;
            }
            m_view->installDisplayPixels(item, image,
                                 SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
    }

    if (GallerySoftState *st = m_view->hostGallerySoftBook().find(path)) {
        st->terminal = true;
        st->have = GallerySoft::maxHave(st->have, m_view->galleryHaveEdgeFromItems(path, nullptr));
    }

    m_view->scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowSliceMs);
    m_view->scheduleGalleryStatusRefresh(GallerySoft::kStatusRefreshMs);
}



void DisplayPipelineController::applyWorkspaceLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (!m_view->isWorkspaceMode() || path.isEmpty() || image.isNull()) {
        return;
    }
    Q_UNUSED(maxEdge);
    // Same soft/display install as Gallery tiles — Workspace items share paths.
    m_view->onImagePreviewLoaded(path, image, loadGate().generation(),
                         static_cast<int>(ImageView::LoadAdd));
    ensureWorkspaceQualityClimb();
}


void DisplayPipelineController::maybeClimbImageModePixelsForView()
{
    // Zoom / resize: PreferCache climbs when on-screen need exceeds painted.
    // Do not start Display@ladder while soft is still missing — that races the
    // soft 512 job and is what THUMTOO_DEBUG showed as need=2048 decoded=0.
    if (!m_view->isImageMode() || m_view->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    ImageItem *item = imageModeItemForPath(m_view->classicPath());
    if (!item) {
        item = m_view->targetItem();
    }
    if (!item || item->path().isEmpty()) {
        return;
    }
    const QString path = item->path();
    // Crop draft freezes the sample — do not schedule soft↔full climb.
    if (m_view->isCropDraftLockedPath(path)) {
        return;
    }

    // Tile LOD owns deep zoom when on-screen need exceeds soft max (TILE_LOD).
    // PreferCache whole-frame climb is skipped for that band; soft/LQIP stays
    // as underlay until tiles arrive.
    m_view->tickPrimaryTileLod(8);
    if (item->tileLodWanted()) {
        m_view->driveImageFocusSurface();
        return;
    }

    const int need = m_view->itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
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
    scheduleImageModePreferCacheClimb(path, need);
    // Soft matching want + large host → ScheduleAsyncMaterialize via decide.
    m_view->driveImageFocusSurface();
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
        m_view->wantAppearanceForItem(item, item->sessionId()));
    if (item->hasDisplayPixels()) {
        ds.haveDisplayEdge = item->displayPixelLongEdge();
        ds.attachedKind = item->hasDecodedPixels()
            ? DisplaySurface::AttachedKind::FullSource
            : DisplaySurface::AttachedKind::SoftPreview;
        if (item->hasAppliedContentXform()) {
            ds.applied = item->appliedContentXform();
        }
    }
    if (m_view->isImageMode()) {
        // ImageFocus: on-screen (window) need only. Forcing need ≥ file native
        // jumped straight to Full and skipped Soft→Prefer progressive installs.
        // Zoom / 1:1 raises on-screen need; climb then escalates.
        const int need = m_view->itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
        ds.needEdge = cappedDisplayEdgeForPath(item->path(), need);
    } else if (m_view->isGalleryMode()) {
        ds.needEdge = m_view->galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
    } else if (m_view->isWorkspaceMode()) {
        ds.needEdge = m_view->itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
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
        if (m_view->isGalleryMode() || !m_view->hostPathRaster()) {
            return false;
        }
        // Image key-repeat: never ensure per skipped path (IMAGE_MODE_NAV_SOFT).
        if (m_view->hostSlideshow().hud().isNavHot() && m_view->isImageMode()) {
            return false;
        }
        const int need = act.climbNeedEdge > 0 ? act.climbNeedEdge : fallbackNeedEdge;
        if (need > 0) {
            m_view->hostPathRaster()->ensure(path, need, m_view->logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::ScheduleAsyncMaterialize) {
        if (m_view->isGalleryMode()) {
            return false;
        }
        if (m_view->hostSlideshow().hud().isNavHot() && m_view->isImageMode()) {
            return false;
        }
        m_view->scheduleAsyncHostRematerialize(
            path, item->sessionId(),
            m_view->wantAppearanceForItem(item, item->sessionId()));
        // Intermediate Prefer host is baking async; keep Soft→Prefer→Full climb
        // when on-screen need is still above host (Workspace zoom-in).
        const int need = fallbackNeedEdge > 0 ? fallbackNeedEdge : 0;
        if (m_view->hostPathRaster() && need > 0
            && !DisplayEdgePolicy::coversEdge(item->displayPixelLongEdge(), need)
            && !DisplayEdgePolicy::coversEdge(ImageCache::longEdge(ImageCache::get(path)), need)) {
            m_view->hostPathRaster()->ensure(path, need, m_view->logicalSizeForPath(path), climbPolicy);
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
        if (!m_view->canAcceptDisplaySample(item, host, kind)) {
            // Gallery LQIP tile: force SoftPreview when host is a real upgrade.
            const int shown = item->displayPixelLongEdge();
            const int hostEdge = ImageCache::longEdge(host);
            if (!(m_view->isGalleryMode()
                  && shown <= DisplayQuality::kLqipMaxEdge
                  && hostEdge > shown)) {
                return false;
            }
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        const QSize before = item->imageSize();
        if (m_view->isImageMode()) {
            installImageModeSampleInPlace(item, path, host, kind);
        } else {
            m_view->installDisplayPixels(item, host, kind, item->sessionId());
            item->update();
            if (m_view->canvasScene()) {
                m_view->canvasScene()->update(item->sceneBoundingRect());
            }
        }
        const bool sizeChanged = (item->imageSize() != before);
        if (act.type == AT::AttachSoft) {
            syncItemDisplaySurface(item, ImageCache::longEdge(host),
                m_view->hostPathRaster() && m_view->hostPathRaster()->isClimbPending(path));
            const DisplaySurface::SurfaceId sid =
                static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
            const DisplaySurface::Action again =
                (sid != DisplaySurface::kInvalidSurfaceId)
                    ? displaySurfaces().evaluate(sid)
                    : DisplaySurface::decide(
                          displaySurfaceStateForItem(
                              item, ImageCache::longEdge(host), false));
            if (again.type == AT::ScheduleAsyncMaterialize) {
                m_view->scheduleAsyncHostRematerialize(
                    path, item->sessionId(),
                    m_view->wantAppearanceForItem(item, item->sessionId()));
            } else if (again.type == AT::ScheduleClimb && m_view->hostPathRaster()
                       && !m_view->isGalleryMode()) {
                const int need = again.climbNeedEdge > 0 ? again.climbNeedEdge
                                                         : fallbackNeedEdge;
                if (need > 0) {
                    m_view->hostPathRaster()->ensure(path, need, m_view->logicalSizeForPath(path),
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
    if (!m_view->isImageMode() || m_view->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    // Key-repeat: install soft only. evaluate() → ScheduleClimb / async bake
    // would pathRaster->ensure every skipped path (bypassed requestEscalateClimb
    // nav-hot guard). Settle loadImage drives the surface once.
    if (m_view->hostSlideshow().hud().isNavHot()) {
        return;
    }
    m_view->syncImageFocusSurfaceState();
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
    ImageItem *item = m_view->primaryItem();
    if (!item || item->path() != path) {
        return;
    }
    SessionImageId sid = b->sessionId;
    if (sid == kInvalidSessionImageId) {
        sid = item->sessionId() != kInvalidSessionImageId ? item->sessionId()
                                                          : m_view->hostSessionId().currentIdValue();
    }

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
    if (m_view->isWorkspaceMode()) {
        kind = DisplaySurface::Kind::WorkspaceItem;
    } else if (m_view->isImageMode()) {
        kind = DisplaySurface::Kind::ImageFocus;
    }
    const DisplaySurface::SurfaceId id = displaySurfaces().bind(
        kind, item->path(), item->sessionId());
    item->setDisplaySurfaceId(id);
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
    // Gallery: LQIP underlay only (≤kLqipMaxEdge). Never soft/HOST whole-frame.
    if (m_view->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview
        && !SoftDisplayPolicy::gallerySoftWithinLqipBand(
               incoming, DisplayQuality::kLqipMaxEdge)) {
        return false;
    }
    if (!item->hasDisplayPixels()) {
        return true; // blank: LQIP-sized SoftPreview only (gated above)
    }
    if (m_view->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        // Only larger LQIP; never soft climb.
        return SoftDisplayPolicy::galleryAcceptsLqipUpgrade(
            item->displayPixelLongEdge(), incoming, DisplayQuality::kLqipMaxEdge);
    }
    DisplaySurface::State ds = displaySurfaceStateForItem(item, incoming, false);
    if (m_view->isCropDraftLockedItem(item) || m_view->isCropDraftLockedPath(item->path())) {
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
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Crop draft owns the live sample — ladder/async must not replace it
    // (store want still has crop → wrong bake; soft↔full thrash).
    if (m_view->isCropDraftLockedItem(item) || m_view->isCropDraftLockedPath(path)) {
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
    if (sid == kInvalidSessionImageId) {
        if (item->sessionId() != kInvalidSessionImageId) {
            sid = item->sessionId();
        } else if (m_view->isImageMode() && m_view->hostSessionId().hasCurrentId()) {
            sid = m_view->hostSessionId().currentIdValue();
        }
    }
    m_view->seedSessionAppearanceFromState(sid, path);

    // Absolute want xform (session store / path map / live flags).
    const WorkspaceItemState appearance = m_view->wantAppearanceForItem(item, sid);

    // Host cache is unoriented. Every ladder/decode sample that enters here is
    // host-raw (Gallery, Image, Workspace). Display-ready stash soft never
    // enters this function — pendingTile attaches it via m_view->attachDisplaySample.
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
    if (m_view->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        pixelsForDisplay = DisplayEdgePolicy::clampSoftForCell(
            pixels,
            m_view->galleryDisplayEdgeForItem(item, /*allowHighRes=*/true),
            ThumtooCache::kFilmstripLadderEdge);
    }
    QImage display = pixelsForDisplay;
    SessionAppearance::PixelKind attachKind = kind;
    bool scheduleFullBake = false;
    if (wantBake) {
        // Key-repeat: never schedule async rematerialize per skipped path —
        // settle loadImage will bake once for the final index.
        const bool navHot = m_view->hostSlideshow().hud().isNavHot() && m_view->isImageMode();
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
                m_view->scheduleAsyncHostRematerialize(path, sid, appearance);
            }
            return;
        }
        if (edge > maxGui) {
            // Clamp failed oddly — still do not attach host under want.
            if (!navHot) {
                m_view->scheduleAsyncHostRematerialize(path, sid, appearance);
            }
            return;
        }
        display = SessionAppearance::materializeDisplay(
            pixelsForDisplay, appearance, attachKind);
        if (display.isNull()) {
            if (!navHot) {
                m_view->scheduleAsyncHostRematerialize(path, sid, appearance);
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
    m_view->attachDisplaySample(item, display, appearance, attachKind);
    if (scheduleFullBake) {
        m_view->scheduleAsyncHostRematerialize(path, sid, appearance);
    }
    // Soft→layout may change aspect; keep Image view scale continuous.
    if (m_view->isImageMode() && item == m_view->targetItem()
        && sizeBeforeAttach != item->imageSize()
        && sizeBeforeAttach.width() > 1 && sizeBeforeAttach.height() > 1) {
        m_view->preserveImageViewOnLogicalSizeChange(item, sizeBeforeAttach, item->imageSize());
    } else if (m_view->isImageMode() && m_view->canvasScene() && m_view->liveItems().size() == 1) {
        m_view->canvasScene()->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    // Do NOT emit sessionAppearanceChanged from decode/install (filmstrip is
    // selection-coupled). Soft ladder upgrades must not rewrite the strip.

    // Gallery reflow only when layout geometry actually changed.
    if (m_view->isGalleryMode() && item->imageSize() != layoutBefore) {
        m_view->requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
}

void DisplayPipelineController::installImageModePendingTile(const QString &path, const QImage &preview)
{

    if (!m_view->isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the m_view->viewport with dwell/live blits. Pending tile used to
    // m_view->clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed. Underlay is hidden for the whole show.
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        return;
    }
    if (m_view->isCropDraftLockedPath(path)) {
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
        if (m_view->liveItems().size() == 1) {
            ImageItem *item = m_view->liveItems().first();
            const bool pathChanged = item->path() != path;
            item->setPath(path);
            m_view->bindImageModeSessionCursor(item);
            if (pathChanged) {
                // Soft OR full — hasDecodedPixels is full-only and left prior soft
                // in place so canAccept rejected the next path's smaller LQIP.
                if (item->hasDisplayPixels()) {
                    item->clearDecodedPixels();
                }
                item->setSessionCrop(false, QRect());
                item->setContentHFlip(false);
                item->setContentVFlip(false);
                item->setColorAdjustmentsRecord(ColorAdjustments{});
                item->clearAppliedContentXform();
                // Intrinsic from size memo/probe when known; else provisional.
                // Paint draws a sized placeholder until LQIP (cache-only) or tiles.
                const QSize sz = m_view->layoutSizeForPath(path, QImage());
                if (isPositiveSize(sz)) {
                    item->setIntrinsicSize(sz);
                    m_view->syncImageModeSceneRect(item);
                }
                // Soft PreferCache encode is removed for Image underlay
                // (LQIP + tiles only). Nav-hot: no IPC — settle loadImage probes
                // size and issues tiles once. Do not scheduleSoftPixels here.
                if (!m_view->hostSlideshow().hud().isNavHot() && ThumtooCache::isAvailable()) {
                    ThumtooCache::scheduleProbe(path);
                }
                if (m_view->viewport()) {
                    m_view->viewport()->update();
                }
                biltooLoadDbg(
                    m_view->hostSlideshow().hud().isNavHot()
                        ? "pendingTile DEFER blank path=%s (nav-hot, placeholder)"
                        : "pendingTile DEFER blank path=%s (placeholder, probe size)",
                    qPrintable(QFileInfo(path).fileName()));
            } else {
                biltooLoadDbg("pendingTile DEFER empty soft path=%s keep prior frame",
                              qPrintable(QFileInfo(path).fileName()));
            }
            return;
        }
        // First image ever: minimal placeholder, no fit storm.
        const QSize sz = m_view->layoutSizeForPath(path, QImage());
        ImageItem *item = m_view->createPlaceholderItem(path, sz);
        if (item) {
            m_view->bindImageModeSessionCursor(item);
            m_view->resetImageModeItemPlacement(item);
            m_view->prepareImageModeCanvas();
        }
        biltooLoadDbg("pendingTile PLACEHOLDER empty soft path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
    }

    // Layout size = native when known; else preview aspect.
    const QSize sz = m_view->layoutSizeForPath(path, pixels);

    // Fast path: reuse the single Image-mode item.
    // Do NOT m_view->setUpdatesEnabled(false) — that defers soft paint until after the
    // whole key handler (chrome + climb schedule); user never sees the soft.
    ImageItem *item = nullptr;
    if (m_view->liveItems().size() == 1) {
        item = m_view->liveItems().first();
    }
    if (item) {
        const QSize sizeBefore = item->imageSize();
        // Capture only when navigating to a different file — same-path soft→HQ
        // upgrades must not replace a user pan with a stale pre-frame anchor.
        const bool pathChanged = (item->path() != path);
        if (pathChanged) {
            m_view->captureStickyPanAnchor(item);
        }
        item->setPath(path);
        m_view->bindImageModeSessionCursor(item);
        // Path change: drop prior sample AND content chrome. m_view->wantAppearanceForItem
        // merges item->sessionHasCrop / contentHFlip when the store slot is empty;
        // leaking the previous image's crop into the new soft is the ←/→ stretch.
        if (pathChanged) {
            // Soft OR full. hasDecodedPixels is full-only; leaving prior soft
            // made canAccept reject the next path's LQIP (shown edge ≥ incoming).
            if (item->hasDisplayPixels()) {
                item->clearDecodedPixels();
            }
            item->setSessionCrop(false, QRect());
            item->setContentHFlip(false);
            item->setContentVFlip(false);
            item->setColorAdjustmentsRecord(ColorAdjustments{});
            item->clearAppliedContentXform();
        } else if (item->hasDecodedPixels()) {
            // Same path soft→HQ: clear full so soft can attach.
            item->clearDecodedPixels();
        }
        // Host-raw soft: installDisplayPixels seeds ImageCache + materializes want.
        // Display-ready (stashed Gallery / filmstrip override) only when it still
        // matches store want — otherwise rematerialize from host so crop/rotate
        // in SessionAppearanceStore are not skipped (stale strip Soft looked like
        // "edits not persistent").
        const WorkspaceItemState want = m_view->wantAppearanceForItem(item, item->sessionId());
        if (displayReady && SessionAppearance::hasContentAppearance(want)) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                pixels = host;
                displayReady = false;
            }
        }
        if (displayReady) {
            m_view->attachDisplaySample(item, pixels, want,
                                SessionAppearance::PixelKind::SoftPreview);
        } else {
            installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }

        // Intrinsic only from definitive file size — never from soft/LQIP sz.
        const QSize known = m_view->logicalSizeForPath(path);
        QSize targetSize = item->imageSize();
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !m_view->isProvisionalImageSize(path)) {
            targetSize = ContentXform::layoutSize(known, want);
        }
        int didFit = 0;
        if (isPositiveSize(targetSize) && targetSize.width() > 1
            && !m_view->isProvisionalImageSize(path)) {
            item->setIntrinsicSize(targetSize);
            const bool needFit =
                sizeBefore.width() <= 1
                || ContentXform::aspectChanged(sizeBefore, targetSize);
            if (needFit || m_view->hostFraming().isStickyZoomEnabled() || m_view->hostFraming().hasPreservedViewScale()) {
                // Aspect change, sticky mode, or free-zoom preserve across files.
                m_view->resetImageModeItemPlacement(item);
                m_view->applyImageModeFraming(item);
                didFit = 1;
            } else if (sizeBefore != targetSize) {
                m_view->preserveImageViewOnLogicalSizeChange(item, sizeBefore, targetSize);
            }
        }
        // Single press: sync repaint so soft is visible before PreferCache.
        // Key-repeat (nav hot): async update only — sync repaint every auto-repeat
        // event was the cumulative GUI freeze under held ←/→.
        if (m_view->viewport()) {
            if (m_view->hostSlideshow().hud().isNavHot()) {
                m_view->viewport()->update();
            } else {
                m_view->viewport()->repaint();
            }
        }
        // Retained path RAM: bind session and paint tiles without waiting for
        // the next coordinator timer (A→B→A should show tiles on this frame).
        if (!m_view->hostSlideshow().hud().isNavHot() && item->tileLodHasPathRam()) {
            item->tickTileLod(8);
        }
        biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d fit=%d painted=%s",
                      qPrintable(QFileInfo(path).fileName()),
                      pixels.width(), pixels.height(), didFit,
                      m_view->hostSlideshow().hud().isNavHot() ? "async" : "sync");
        return;
    }

    // No reusable item — still try to capture from whatever was on the canvas.
    if (!m_view->liveItems().isEmpty()) {
        m_view->captureStickyPanAnchor(m_view->liveItems().first());
    }
    m_view->clearLiveCanvas();
    item = m_view->createPlaceholderItem(path, sz);
    if (!item) {
        m_view->setUpdatesEnabled(true);
        return;
    }
    m_view->bindImageModeSessionCursor(item);
    installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                         m_view->hostSessionId().currentIdValue());
    m_view->resetImageModeItemPlacement(item);
    m_view->prepareImageModeCanvas();
    m_view->applyImageModeFraming(item);
    m_view->setUpdatesEnabled(true);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
    biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height());
}

void DisplayPipelineController::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    // Suppress paints between removing the old item and fitting the new one
    // so we never present a native-scale (or empty) intermediate frame.
    m_view->setUpdatesEnabled(false);
    // Preserve sticky pan across the wipe (soft→full or cold replace).
    if (!m_view->liveItems().isEmpty()) {
        if (m_view->liveItems().first()->path() != path) {
            m_view->captureStickyPanAnchor(m_view->liveItems().first());
        } else if (m_view->hostFraming().isStickyZoomEnabled()
                   && !m_view->hostFraming().isStickyFit()) {
            // Same path rebuild: keep looking where we are now.
            m_view->captureStickyPanAnchor(m_view->liveItems().first());
        }
    }
    // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
    m_view->clearLiveCanvas();
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        m_view->setUpdatesEnabled(true);
        m_view->hostSessionId().setLastLoadError(path);
        emit m_view->statusChanged();
        return;
    }
    // Filmstrip overrides are not driven by decode (selection/nav).
    // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
    // Arbitrary Workspace rotation stays on the free-form item only.
    // m_view->createItemFromImage materializes host × store want (install invariant).
    m_view->bindImageModeSessionCursor(item);
    m_view->resetImageModeItemPlacement(item);
    applyLegacyPathFlipsIfNeeded(item, path);
    m_view->prepareImageModeCanvas();
    m_view->frameImageModeReplaceItem(item, path);
    m_view->setUpdatesEnabled(true);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
}

void DisplayPipelineController::completeLoadReplace(const QString &path, const QImage &image, quint64 generation)
{
    if (generation != loadGate().generation()) {
        return; // superseded by a newer navigation / open
    }
    // Stale navigation: only the current classic path may install.
    // Empty multi-item canvas can still seed from m_view->classicPath.
    if (path != m_view->classicPath()) {
        return;
    }
    if (image.isNull()) {
        if (ThumtooCache::isAvailable()) {
            // Full native miss: PreferCache display ladder so onLadderReady can
            // upgrade Image mode (soft→HQ). Skip when tiles already own zoom.
            bool tilesOwn = false;
            if (ImageItem *it = imageModeItemForPath(path)) {
                tilesOwn = it->tileLodWanted();
            }
            if (!tilesOwn) {
                scheduleImageModePreferCacheClimb(path, ThumtooCache::kBatchOverviewEdge);
            }
            m_view->hostSessionId().clearLastLoadError();
        } else {
            m_view->hostSessionId().setLastLoadError(path);
        }
        emit m_view->statusChanged();
        return;
    }
    if (m_view->isImageMode()) {
        (void)tryInstallImageModeSample(path, image);
        return;
    }
    m_view->seedEmptyWorkspaceFromReplace(path, image);
}

void DisplayPipelineController::finishLoadAddStatus(bool refreshGalleryWindow)
{
    emit m_view->statusChanged();
    if (refreshGalleryWindow && m_view->isGalleryMode()) {
        m_view->scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowSettleMs);
    }
}


bool DisplayPipelineController::acceptPendingLoadAdd(const QString &path, quint64 generation)
{
    // Mode leave / empty Workspace bumps generation and clears pending paths.
    // Reject superseded gallery window decodes so they cannot spawn tiles on
    // Workspace after the user switched modes mid-decode.
    if (generation != loadGate().generation()) {
        finishLoadAddStatus(/*refreshGalleryWindow=*/false);
        return false;
    }
    if (!loadGate().containsPendingWorkspacePath(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return false;
    }
    m_view->takePendingWorkspacePath(path);
    return true;
}


void DisplayPipelineController::handleLoadAddDecodeFailure(const QString &path)
{
    // //pdfimage: / //page: with thumtoo: empty sync load is expected while the
    // ladder builds — await ladderReady instead of permanent failure.
    if (ThumtooCache::isAvailable()
        && (PagePath::isPdfImageRef(path) || PagePath::isPageRef(path))) {
        ThumtooCache::scheduleProbe(path);
        // Soft state machine will request placeholder / higher steps.
        m_view->hostSessionId().clearLastLoadError();
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return;
    }
    qWarning("ImageView: decode failed for %s", qPrintable(path));
    if (m_view->isGalleryMode()) {
        GallerySoftState &st = m_view->hostGallerySoftBook().state(path);
        st.failed = true;
        st.inflight = 0;
    }
    m_view->hostSessionId().setLastLoadError(path);
    // Surface the error on any live placeholder for this path.
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            item->setToolTip(m_view->tr("Failed to load:\n%1").arg(path));
        }
    }
    finishLoadAddStatus(/*refreshGalleryWindow=*/true);
}


void DisplayPipelineController::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    for (ImageItem *cand : m_view->hostGallery().stashedItems()) {
        if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
            installDisplayPixels(cand, image,
                                 SessionAppearance::PixelKind::FullSource,
                                 cand->sessionId());
        }
    }
}


void DisplayPipelineController::reassertPendingBindPlacement(const QString &path)
{
    // Drop placeholders created in placeOrMoveImageAt already sit at scenePos;
    // re-assert hasScenePos binds so nothing later drifts them, and so a bind
    // still pairs with the pre-created tile.
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        for (int bi = 0; bi < m_view->hostBindBook().bindCount(); ++bi) {
            const PendingSessionBind &b = m_view->hostBindBook().bindAt(bi);
            if (b.path != path) {
                continue;
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() != kInvalidSessionImageId
                && b.id != item->sessionId()) {
                continue;
            }
            if (b.hasScenePos) {
                item->setGalleryCellSize({});
                item->setPos(b.scenePos);
                item->setItemScale(1.0);
                item->setItemRotation(0.0);
                item->setItemShear(0.0);
                item->setItemOpacity(1.0);
                if (m_view->isWorkspaceMode()) {
                    item->setInteractive(true);
                    item->setScaleHandlesEnabled(true);
                }
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
                item->setSessionId(b.id);
            }
            if (b.index >= 0 && item->sessionIndex() < 0) {
                item->setSessionIndex(b.index);
            }
            break;
        }
    }
}


void DisplayPipelineController::claimUnboundItemsForPendingBinds(const QString &path, const QImage &image)
{
    // Claim existing *unbound* tiles of this path for pending session binds
    // (e.g. empty-Workspace LoadReplace seeded the first path before LoadAdd).
    // Without this, have==wanted and the bind is never applied — placement,
    // content flips, and colour grade stay at defaults on that tile.
    for (ImageItem *existing : m_view->liveItems()) {
        if (!existing || existing->path() != path) {
            continue;
        }
        if (existing->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        PendingSessionBind bound;
        if (!m_view->takePendingSessionBind(path, &bound)) {
            break;
        }
        if (bound.id != kInvalidSessionImageId) {
            existing->setSessionId(bound.id);
        }
        if (bound.index >= 0 && bound.id != kInvalidSessionImageId) {
            existing->setSessionIndex(bound.index);
        }
        // Raw full decode → single appearance gate (seed tiles may already
        // have decoded defaults without content ops).
        installDisplayPixels(existing, image,
                             SessionAppearance::PixelKind::FullSource,
                             bound.id != kInvalidSessionImageId
                                 ? bound.id
                                 : existing->sessionId());
        if (bound.id != kInvalidSessionImageId && m_view->appearance().get(bound.id)) {
            m_view->applyState(existing, *m_view->appearance().get(bound.id));
        }
        // Explicit drop position wins over restored gallery/workspace pose.
        m_view->applyPendingBindScenePos(existing, bound);
        if (bound.id != kInvalidSessionImageId) {
            // Decode must not rewrite filmstrip (sessionAppearanceChanged).
            if (m_view->hostBindBook().removeSelectId(bound.id)) {
                existing->setSelected(true);
            }
        }
    }
}


int DisplayPipelineController::fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                              bool *sizeChangedOut)
{
    bool sizeChanged = false;
    int have = 0;
    const int incoming = ImageCache::longEdge(image);
    for (ImageItem *existing : m_view->liveItems()) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        // Soft was wrongly stored as "decoded"; still accept stricter long edge.
        if (!existing->hasDecodedPixels()
            || existing->shouldUpgradeDisplayTo(incoming)) {
            if (m_view->installFullPreservingWorkspaceFootprint(existing, image)) {
                sizeChanged = true;
            } else if (existing->shouldUpgradeDisplayTo(incoming)) {
                // Footprint helper no-ops once hasDecodedPixels; force upgrade.
                installDisplayPixels(existing, image,
                                     SessionAppearance::PixelKind::FullSource,
                                     existing->sessionId());
                existing->update();
                sizeChanged = true;
            }
        }
    }
    if (sizeChangedOut) {
        *sizeChangedOut = sizeChanged;
    }
    return have;
}


void DisplayPipelineController::createMissingLoadAddItems(const QString &path, const QImage &image,
                                          int have, int wanted)
{
    if (m_view->gallerySizeResolveActive() || m_view->hostGallerySoftBook().isDeferPopulate()) {
        return;
    }
    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {
        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        PendingSessionBind bound;
        const bool haveBound = m_view->takePendingSessionBindForNewItem(path, item, &bound);
        m_view->applyStoredAppearance(item);
        // Decode/membership must not rewrite filmstrip; user edits emit overrides.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            // Paste: select tiles as they finish decoding.
            if (m_view->hostBindBook().removeSelectId(bound.id)) {
                item->setSelected(true);
            }
        }
        m_view->placeNewLoadAddItem(item, path, image, haveBound, bound);
    }
}


void DisplayPipelineController::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    if (m_view->gallerySizeResolveActive() || m_view->hostGallerySoftBook().isDeferPopulate()) {
        return;
    }
    if (!m_view->hostLayout().isFreeForm()) {
        if (!m_view->pathOrderIsEmpty()) {
            m_view->reorderItemsByPaths(m_view->pathOrderPaths());
        }
        if (!(m_view->isGalleryMode() && m_view->hostGalleryRelayoutSuppress().active())) {
            if (sizeChanged) {
                m_view->applyLayout(GalleryPackReason::ContentChange);
            } else {
                m_view->applyLayout(GalleryPackReason::SessionMutate);
            }
        }
    } else {
        m_view->updateWorkspaceSceneRect();
    }
}


void DisplayPipelineController::completeLoadAdd(const QString &path, const QImage &image, quint64 generation)
{
    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window.
    // Duplicate paths are separate session images: fill every undecoded live
    // occurrence, then create until live count matches pathOrder occurrences.
    m_view->gallerySoftResetPath(path);

    // Remember size even when the pending membership was cancelled — a successful
    // decode still updates the session size cache for later layout.
    if (generation == loadGate().generation() && !image.isNull()) {
        m_view->rememberSizeFromDecode(path, image);
    }
    if (!acceptPendingLoadAdd(path, generation)) {
        return;
    }
    if (image.isNull()) {
        handleLoadAddDecodeFailure(path);
        return;
    }

    if (m_view->isImageMode()) {
        // Fill stashed Gallery placeholders while user is in Image mode.
        fillStashedItemsForPath(path, image);
        emit m_view->statusChanged();
        return;
    }

    reassertPendingBindPlacement(path);

    const int pathOrderCount = m_view->pathOrderOccurrences(path);

    // Pending binds whose SessionImageId is already on a live tile are satisfied.
    m_view->purgeSatisfiedPendingBinds(path);

    claimUnboundItemsForPendingBinds(path, image);

    const int pendingBinds = m_view->countPendingSessionBinds(path);

    bool sizeChanged = false;
    int have = fillLiveItemsWithDecodedPixels(path, image, &sizeChanged);
    fillStashedItemsForPath(path, image);

    // Session pathOrder is the multiplicity source of truth. Do not create more
    // tiles than session rows for this path (pending binds only fill gaps).
    int wanted = pathOrderCount;
    if (wanted <= 0) {
        // Not in session pathOrder (ad-hoc workspace place): one tile per bind.
        wanted = DisplayEdgePolicy::wantedBindCount(have, pendingBinds);
    }

    createMissingLoadAddItems(path, image, have, wanted);
    applyLoadAddLayoutAfterMembership(sizeChanged);

    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    if (m_view->isGalleryMode()) {
        m_view->scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowSettleMs);
    }
    if (m_view->isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
}

void DisplayPipelineController::scheduleImageLoad(const QString &path, int role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == ImageView::LoadAdd) {
        m_view->addPendingWorkspacePath(path);
    }
    // ImageView::LoadRestore pending is owned by loadGate().pendingRestoreStates() (AUDIT M27).
    // AUDIT H3a: only ImageView::LoadReplace advances the generation token so workspace
    // adds cannot cancel an in-flight Image-mode navigation decode.
    quint64 gen = loadGate().generation();
    if (role == ImageView::LoadReplace) {
        gen = loadGate().bumpGeneration();
        m_view->hostGallerySoftBook().clearImageModeNativeDecode();
        // Do NOT setPrimaryInterest here — that starts EnsureTiles / FocusFull
        // pyramid builds on archives and cancels the soft queue every ←/→.
    }
    // ImageView::LoadReplace: do NOT emit statusChanged — MainWindow finishCurrentIndexChromeUpdate
    // already updateStatus(); a second statusChanged re-entered updateStatus and
    // rebuilt chrome on every ←/→ (statusText, metadata, adjustments, filmstrip pending).

    // Slideshow dual-blit already decoded this path — reuse under the hold.
    if (role == ImageView::LoadReplace && tryDeliverReplaceFromSlideshowRaster(path, gen)) {
        return;
    }

    // Slideshow owns the viewport via pure-phase buffers — never soft-install
    // or PreferCache-climb the underlay ImageItem while the show is running.
    if (role == ImageView::LoadReplace && m_view->isImageMode() && m_view->hostSlideshow().hud().isProgressActive()) {
        biltooLoadDbg("PATH slideshow active skip image-mode load path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
    }

    // Image mode: swap best in-process soft/LQIP immediately (no IPC, no pool).
    if (role == ImageView::LoadReplace && m_view->isImageMode()) {
        installImageModePendingTile(path);
        // Rapid ←/→: stop here. No PreferCache, no classic decode, no escalate —
        // those race the next key and stall the GUI. Settle timer (MainWindow
        // ~80ms quiet) clears nav-hot and calls loadImage again for climb.
        if (m_view->hostSlideshow().hud().isNavHot()) {
            const int edge = imageModeItemForPath(path)
                ? imageModeItemForPath(path)->displayPixelLongEdge()
                : 0;
            biltooLoadDbg("PATH nav-hot soft-only path=%s edge=%d",
                          qPrintable(QFileInfo(path).fileName()), edge);
            return;
        }
        if (ImageItem *it = imageModeItemForPath(path)) {
            if (it->displayPixelLongEdge() > 0) {
                const QImage soft = it->displayImage();
                const QString pathCopy = path;
                // One frame for soft paint, then PreferCache for the settled path.
                QTimer::singleShot(16, m_view, [this, pathCopy, soft]() {
                    if (!m_view->isImageMode() || m_view->classicPath() != pathCopy) {
                        return;
                    }
                    if (m_view->hostSlideshow().hud().isNavHot()) {
                        return;
                    }
                    ensureImageModeQualityClimb(pathCopy, soft);
                });
                return;
            }
        }
    }

    // Image mode: do not SoftOnly/PreferCache on cold open — LQIP (if cached)
    // + tiles. Other modes still seed Soft via scheduleClassicImageDecode.

    if (m_view->hostSlideshow().hud().isProgressActive()) {
        scheduleSlideshowReplaceDecode(path, gen, role);
        return;
    }
    scheduleClassicImageDecode(path, gen, role);
}


bool DisplayPipelineController::tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen)
{
    const QImage ready = m_view->hostSlideshow().slideshowRaster(path);
    if (ready.isNull()) {
        return false;
    }
    const QPointer<ImageView> guard(this);
    QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                              Q_ARG(QString, path),
                              Q_ARG(QImage, ready),
                              Q_ARG(quint64, gen),
                              Q_ARG(int, static_cast<int>(ImageView::LoadReplace)));
    return true;
}


void DisplayPipelineController::scheduleSlideshowReplaceDecode(const QString &path, quint64 gen, int role)
{
    // Soft first (priority), then PreferCache at target edge (low).
    // Key-repeat skips loadImage entirely (MainWindow debounce); this path is
    // for settled index / auto-advance — must climb above soft max or the show
    // stays on thumbnails forever.
    const QPointer<ImageView> guard(this);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const int qualityEdge = m_view->hostSlideshow().slideshowTargetEdge();
    const int roleInt = static_cast<int>(role);
    // Snapshot session appearance for the worker (crop is id-keyed, not path).
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);

    // Warm ImageCache / durable tiles: no SoftOnly encode.
    {
        const QImage cached = ImageCache::get(path);
        const int have = ImageCache::longEdge(cached);
        if (have > 0) {
            ImageCache::put(path, cached);
            queuePreviewLoaded(guard, path, cached, gen, roleInt);
            if (ImageCache::adequate(cached, qualityEdge)) {
                queueImageLoaded(guard, path, cached, gen, roleInt);
                return;
            }
        }
        if (ThumtooCache::hasDurableTilesKnown(path)) {
            tickPrimaryTileLod(12);
            if (qualityEdge > softEdge) {
                startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge,
                                       sessionApp);
            }
            return;
        }
        if (have > 0 && ImageCache::adequate(cached, softEdge)) {
            if (qualityEdge > softEdge) {
                startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge,
                                       sessionApp);
            }
            return;
        }
    }

    // Cold only: soft stand-in then quality job.
    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    if (qualityEdge > softEdge) {
        startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge, sessionApp);
    }
}


void DisplayPipelineController::scheduleClassicImageDecode(const QString &path, quint64 gen, int role)
{
    // Image mode: LQIP/cache underlay only if already present, then tiles.
    // No SoftOnly encode and no PreferCache/Full climb in parallel with tiles.
    if (m_view->isImageMode() && !m_view->hostSlideshow().hud().isProgressActive() && !m_view->hostSlideshow().hud().isNavHot()
        && role == ImageView::LoadReplace) {
        ThumtooCache::scheduleProbe(path);
        tickPrimaryTileLod(12);
        Q_UNUSED(gen);
        return;
    }

    // Gallery / Workspace: LQIP from ImageCache + tiles. Never SoftOnly job.
    if (m_view->isGalleryMode() || m_view->isWorkspaceMode()) {
        ThumtooCache::scheduleProbe(path);
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull()
            && ImageCache::longEdge(cached) <= DisplayQuality::kLqipMaxEdge) {
            const QPointer<ImageView> guard(this);
            queuePreviewLoaded(guard, path, cached, gen, static_cast<int>(role));
        }
        if (m_view->isGalleryMode()) {
            scheduleGalleryDecode(path);
        }
        tickPrimaryTileLod(12);
        Q_UNUSED(role);
        return;
    }

    // Fallback (rare non-mode): soft stand-in — prefer cache LQIP first.
    const QPointer<ImageView> guard(this);
    const int roleInt = static_cast<int>(role);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);
    {
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull()
            && ImageCache::longEdge(cached) <= DisplayQuality::kLqipMaxEdge) {
            queuePreviewLoaded(guard, path, cached, gen, roleInt);
            tickPrimaryTileLod(8);
            Q_UNUSED(sessionApp);
            return;
        }
    }
    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    Q_UNUSED(gen);
}



void DisplayPipelineController::gallerySoftResetPath(const QString &path)
{
    m_view->hostGallerySoftBook().resetPath(path);
    if (m_view->hostPathRaster() && !path.isEmpty()) {
        m_view->hostPathRaster()->cancel(path);
    }
}


void DisplayPipelineController::gallerySoftResetAll()
{
    m_view->hostGallerySoftBook().clearSoft();
}


int DisplayPipelineController::galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const
{
    // Collect edges for this path, then pure aggregate (SoftDisplayPolicy).
    QVarLengthArray<int, 8> edges;
    QVarLengthArray<bool, 8> decoded;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        edges.append(item->displayPixelLongEdge());
        decoded.append(item->hasDecodedPixels());
    }
    const SoftDisplayPolicy::PathHaveEdge agg =
        SoftDisplayPolicy::aggregatePathHaveEdge(
            edges.constData(), decoded.constData(), edges.size());
    if (anyFullOut) {
        *anyFullOut = agg.anyFull;
    }
    return agg.have;
}



void DisplayPipelineController::scheduleGalleryDecode(const QString &path)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("scheduleGalleryDecode", 2);
    if (!m_view->isGalleryMode() || path.isEmpty()) {
        return;
    }
    // Gallery: LQIP placeholder + tiles only. Soft PreferCache is removed.

    if (m_view->isProvisionalImageSize(path) || !ThumtooCache::cachedSize(path).isValid()) {
        m_view->scheduleImageSizeProbe(path);
    }

    bool anyTileWanted = false;
    bool needLqip = false;
    for (ImageItem *ii : m_view->liveItems()) {
        if (!ii || ii->path() != path) {
            continue;
        }
        if (ii->tileLodWanted()) {
            anyTileWanted = true;
        }
        if (!ii->hasDisplayPixels()) {
            needLqip = true;
        }
    }

    if (needLqip) {
        // ImageCache only (warmSessionOpenMemos / probe workers put LQIP there).
        const QImage host = ImageCache::get(path);
        if (!host.isNull()
            && ImageCache::longEdge(host) <= DisplayQuality::kLqipMaxEdge) {
            for (ImageItem *ii : m_view->liveItems()) {
                if (!ii || ii->path() != path || ii->hasDisplayPixels()) {
                    continue;
                }
                installDisplayPixels(ii, host,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     ii->sessionId());
            }
        }
    }

    GallerySoftState &st = m_view->hostGallerySoftBook().state(path);
    st.terminal = true; // no soft climb ever
    st.have = GallerySoft::maxHave(st.have, galleryHaveEdgeFromItems(path, nullptr));

    if (anyTileWanted && !m_view->gallerySizeResolveActive()) {
        // Size must be known before pyramid encode (expensive). Wait for resolve.
        if (!ThumtooCache::cachedSize(path).isValid()) {
            return;
        }
        // Only encode a pyramid when Store has no durable coverage yet.
        if (!st.isTilesPyramidQueued()) {
            st.markTilesPyramidQueued();
            if (!ThumtooCache::hasDurableTilesKnown(path)) {
                (void)ThumtooCache::scheduleTilePyramid(path);
            }
        }
    }
}

void DisplayPipelineController::onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                                     int role)
{
    if (image.isNull()) {
        return;
    }
    ImageCache::put(path, image);
    // Soft job during slideshow must upgrade phase buffers (m_ssFrom/To), not
    // only ImageCache — otherwise crossfade stays on empty/LQIP until preload.
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        m_view->hostSlideshow().onSlideshowRasterReady(path, image);
    }

    // Replace navigations: drop superseded previews.
    if (role == ImageView::LoadReplace) {
        if (generation != loadGate().generation() || path != m_view->classicPath()) {
            return;
        }
        if (m_view->isImageMode()) {
            // Same install + climb policy as completeImageView::LoadReplace / ladderReady.
            (void)tryInstallImageModeSample(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }

    const int incoming = ImageCache::longEdge(image);

    // Gallery: LQIP placeholder only. Soft PreferCache deliveries must not
    // climb Gallery cells (filmstrip soft used SoftDisplay here).
    if (m_view->isGalleryMode()) {
        if (incoming > 0 && incoming <= DisplayQuality::kLqipMaxEdge) {
            for (ImageItem *item : m_view->liveItems()) {
                if (!item || item->path() != path || item->hasDisplayPixels()) {
                    continue;
                }
                installDisplayPixels(item, image,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     item->sessionId());
            }
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
        }
        return;
    }

    // Workspace: DisplaySurface::decide per item.
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        const bool climbPending =
            m_view->hostPathRaster() && m_view->hostPathRaster()->isClimbPending(path);
        syncItemDisplaySurface(item, incoming, climbPending);
        DisplaySurface::State ds =
            displaySurfaceStateForItem(item, incoming, climbPending);
        const DisplaySurface::SurfaceId sid =
            static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
        const DisplaySurface::Action act =
            (sid != DisplaySurface::kInvalidSurfaceId)
                ? displaySurfaces().evaluate(sid)
                : DisplaySurface::decide(ds);
        const auto pol =
            (!ThumtooCache::hasDurableTilesKnown(path))
                ? PathRasterService::ClimbPolicy::EscalateToFull
                : PathRasterService::ClimbPolicy::SoftDisplay;
        (void)applyDisplaySurfaceAction(item, act, image, ds.needEdge, pol);
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    if (m_view->isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
}


bool DisplayPipelineController::takePendingRestoreState(const QString &path, WorkspaceItemState *out)
{
    return loadGate().takePendingRestoreForPath(path, out);
}


void DisplayPipelineController::completeLoadRestore(const QString &path, const QImage &image)
{
    WorkspaceItemState state;
    if (!takePendingRestoreState(path, &state) || image.isNull()) {
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
        if (const WorkspaceItemState *it = appearance().get(state.sessionId)) {
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
    // Host is in ImageCache / item. Materialize store want (soft stand-in +
    // async multi-MP). Do not bake chrome-only on multi-MP — cannot
    // bake crop on the GUI and used to claim applied == want without pixels.
    if (SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity()) {
        rematerializeItemContent(item, app);
    }
    m_view->applyState(item, app);
    if (!m_layout.isFreeForm()
        && !(m_view->isGalleryMode() && m_galleryRelayoutSuppress.active())) {
        applyLayout(GalleryPackReason::SessionMutate);
    }
    emit m_view->statusChanged();
    emit workspacePathsChanged();
}

WorkspaceItemState DisplayPipelineController::appearanceForNewImageModeItem(const QString &path)
{
    // Prefer stable session-image id appearance; path map is legacy only.
    //
    // Image mode LoadReplace: the sole canvas item is the current session
    // image, so m_view->hostSessionId().currentIdValue() identifies it correctly.
    //
    // Gallery / Workspace LoadAdd must not call this: each tile is bound to
    // its own session id *after* creation. Applying m_view->hostSessionId().currentIdValue() here
    // would bake the navigated image's crop into every newly decoded tile.
    if (m_view->hostSessionId().hasCurrentId()) {
        seedSessionAppearanceFromState(m_view->hostSessionId().currentIdValue(), path);
        if (const WorkspaceItemState *sit = m_view->appearance().get(m_view->hostSessionId().currentIdValue())) {
            return *sit;
        }
        // Bound session image with no appearance entry = full frame, no path fallback.
        return {};
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_view->hostItemStateBook().get(path)) {
        return *st;
    }
    return {};
}


ImageItem *DisplayPipelineController::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    // @p image is always host-raw (workers no longer bake). Size-first ctor;
    // never QPixmap::fromImage of multi-MP in ImageItem(path, image).
    WorkspaceItemState app;
    if (applyStoredSessionCrop && m_view->isImageMode()) {
        // Always attempt seed from path XDG when bound. The old gate
        // !(haveId && !m_view->appearance().get(id)) *skipped* seed when the slot was
        // empty — which is exactly when durable rotate/flip must be loaded
        // after restart. appearanceForNewImageModeItem seeds then returns
        // identity only if XDG has nothing.
        if (m_view->hostSessionId().hasCurrentId()
            || m_view->hostItemStateBook().contains(path)) {
            app = appearanceForNewImageModeItem(path);
        }
    }
    // Logical size only from probe / map — never sample (LQIP/soft) dims.
    QSize native = m_view->layoutSizeForPath(path, QImage());
    if (m_view->isProvisionalImageSize(path)
        || !isPositiveSize(native) || native.width() <= 1 || native.height() <= 1) {
        // Cold: 1×1 until sizeReady; soft install must not invent geometry.
        native = QSize(1, 1);
        m_view->scheduleImageSizeProbe(path);
    }
    QSize intrinsic = ContentXform::layoutSize(native, app);
    if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
        intrinsic = QSize(1, 1);
    }

    auto *item = new ImageItem(path, intrinsic);
    m_view->applyItemModeFlags(item);
    m_view->canvasScene()->addItem(item);
    m_view->liveItems().append(item);
    registerItemDisplaySurface(item);

    // @p image is host-raw. Sole materialize site is installDisplayPixels.
    const bool wantBake = SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity();
    if (wantBake) {
        // Seed item chrome so wantAppearanceForItem can merge if the store slot
        // is still empty (bound id with no entry yet).
        item->setContentHFlip(app.contentHFlip);
        item->setContentVFlip(app.contentVFlip);
        item->setSessionCrop(app.hasCrop, app.cropRect);
        item->setColorAdjustmentsRecord(app.colorAdjust);
        const SessionImageId sid = m_view->isImageMode()
            ? m_view->hostSessionId().currentIdValue()
            : kInvalidSessionImageId;
        const int hostEdge = ImageCache::longEdge(image);
        const auto kind = (hostEdge > ThumtooCache::kGalleryLadderEdge)
            ? SessionAppearance::PixelKind::FullSource
            : SessionAppearance::PixelKind::SoftPreview;
        installDisplayPixels(item, image, kind, sid);
    } else {
        if (!path.isEmpty()) {
            ImageCache::put(path, image);
        }
        // Always installDisplayPixels so seed + materialize run. Skipping that
        // for Image-mode Soft (setPreviewImage) left durable orient unapplied.
        const int hostEdge = ImageCache::longEdge(image);
        const auto kind = (hostEdge > ThumtooCache::kGalleryLadderEdge)
            ? SessionAppearance::PixelKind::FullSource
            : SessionAppearance::PixelKind::SoftPreview;
        const SessionImageId sid = m_view->isImageMode()
            ? m_view->hostSessionId().currentIdValue()
            : item->sessionId();
        installDisplayPixels(item, image, kind, sid);
    }
    return item;
}



void DisplayPipelineController::seedSessionAppearancesFromPaths(const QStringList &paths,
                                                   const QVector<SessionImageId> &ids)
{
    // Fresh session: allow seed again for new ids (old set cleared on invalidate).
    const int n = ViewTransform::pairCount(paths.size(), ids.size());
    for (int i = 0; i < n; ++i) {
        m_view->appearance().clearSeedAttempted(ids.at(i));
    }
    // Small sessions: fine on GUI (few stats). Large sessions: locatorId +
    // appearance SQLite used to run O(n) on the GUI during open and freeze the
    // event loop while the HUD still said "Reading file info…".
    if (n <= 24) {
        for (int i = 0; i < n; ++i) {
            if (ids.at(i) == kInvalidSessionImageId || paths.at(i).isEmpty()) {
                continue;
            }
            seedSessionAppearanceFromState(ids.at(i), paths.at(i));
        }
        return;
    }
    QVector<SessionImageId> idsCopy = ids.mid(0, n);
    QStringList pathsCopy = paths.mid(0, n);
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, pathsCopy, idsCopy]() {
        struct Hit {
            SessionImageId sid = kInvalidSessionImageId;
            QString path;
            ThumtooCache::StoredContentAppearance stored;
        };
        QVector<Hit> hits;
        hits.reserve(pathsCopy.size());
        // Every examined sid must be marked attempted on the GUI (including
        // load miss / identity) so wantAppearanceForItem does not re-drive
        // locatorId on every paint.
        QVector<SessionImageId> attempted;
        attempted.reserve(pathsCopy.size());
        for (int i = 0; i < pathsCopy.size(); ++i) {
            if (idsCopy.at(i) == kInvalidSessionImageId || pathsCopy.at(i).isEmpty()) {
                continue;
            }
            attempted.push_back(idsCopy.at(i));
            ThumtooCache::StoredContentAppearance stored;
            if (!ThumtooCache::loadContentAppearance(pathsCopy.at(i), &stored)) {
                continue;
            }
            if (stored.isIdentity()) {
                continue;
            }
            hits.push_back(Hit{idsCopy.at(i), pathsCopy.at(i), stored});
        }
        if (hits.isEmpty() && attempted.isEmpty()) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, hits, attempted]() {
            ImageView *host = guard.data();
            if (!host) {
                return;
            }
            for (const SessionImageId sid : attempted) {
                host->markAppearanceSeedAttempted(sid);
            }
            for (const Hit &h : hits) {
                host->applyStoredContentAppearanceSeed(h.sid, h.path, h.stored);
            }
        }, Qt::QueuedConnection);
    });
}


void DisplayPipelineController::seedSessionAppearanceFromState(SessionImageId sid, const QString &path)
{
    if (sid == kInvalidSessionImageId || path.isEmpty()) {
        return;
    }
    // One attempt per session id — archive/miss paths must not re-hit locatorId
    // on every paint via wantAppearanceForItem.
    if (m_view->appearance().seedAttempted(sid)) {
        return;
    }
    m_view->appearance().markSeedAttempted(sid);
    ThumtooCache::StoredContentAppearance stored;
    if (!ThumtooCache::loadContentAppearance(path, &stored)) {
        return;
    }
    if (stored.isIdentity()) {
        return;
    }
    applyStoredContentAppearanceSeed(sid, path, stored);
}


void DisplayPipelineController::markAppearanceSeedAttempted(SessionImageId sid)
{
    if (sid != kInvalidSessionImageId) {
        m_view->appearance().markSeedAttempted(sid);
    }
}


void DisplayPipelineController::applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                                 const ThumtooCache::StoredContentAppearance &stored)
{
    if (sid == kInvalidSessionImageId || path.isEmpty() || stored.isIdentity()) {
        return;
    }
    // Worker path may not have marked attempted yet; mark here so paint does not
    // re-drive locatorId via wantAppearanceForItem.
    m_view->appearance().markSeedAttempted(sid);
    if (m_view->appearance().contains(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (const WorkspaceItemState *cur = m_view->appearance().get(sid)) {
            if (SessionAppearance::hasContentAppearance(*cur)) {
                return;
            }
        }
    }
    WorkspaceItemState seed;
    seed.sessionId = sid;
    seed.path = path;
    // Orient/flip/grade only. Crop is per SessionImageId — never seed from
    // path-keyed XDG (duplicates share a path; last crop would leak).
    seed.contentHFlip = stored.contentHFlip;
    seed.contentVFlip = stored.contentVFlip;
    seed.contentQuarterTurns = stored.contentQuarterTurns;
    if (stored.hasGrade) {
        // Durable gradeGamma is percent (100 = 1.0); 0 contrast/sat = identity 100.
        seed.colorAdjust = ColorAdjustments::fromDurableGrade(
            stored.gradeBrightness, stored.gradeContrast, stored.gradeSaturation,
            stored.gradeHue, stored.gradeGamma, stored.gradeInvert);
    }
    m_view->appearance().set(sid, seed);
}


void DisplayPipelineController::installDisplayPreservingView(ImageItem *item, const QImage &pixels,
                                             SessionAppearance::PixelKind kind,
                                             SessionImageId sid)
{
    if (!item || pixels.isNull()) {
        return;
    }
    const QSize before = item->imageSize();
    if (sid == kInvalidSessionImageId) {
        sid = item->sessionId() != kInvalidSessionImageId
                  ? item->sessionId()
                  : m_view->hostSessionId().currentIdValue();
    }
    installDisplayPixels(item, pixels, kind, sid);
    preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
}


WorkspaceItemState DisplayPipelineController::wantAppearanceForItem(const ImageItem *item,
                                                      SessionImageId sid) const
{
    WorkspaceItemState want;
    if (!item) {
        return want;
    }
    SessionImageId id = sid;
    if (id == kInvalidSessionImageId) {
        id = item->sessionId();
    }
    if (id == kInvalidSessionImageId && m_view->isImageMode()) {
        id = m_view->hostSessionId().currentIdValue();
    }
    if (id != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = m_view->appearance().get(id)) {
            want = *app;
        }
        // Cold open / ←→: id slot often empty until first seed. Path XDG holds
        // durable rotate/flip/grade — pull it before materialize or we paint
        // unoriented host forever.
        if (!SessionAppearance::hasContentAppearance(want)
            && want.colorAdjust.isIdentity()
            && !item->path().isEmpty()) {
            const_cast<DisplayPipelineController *>(this)->seedSessionAppearanceFromState(
                id, item->path());
            if (const WorkspaceItemState *app = m_view->appearance().get(id)) {
                want = *app;
            }
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_view->hostItemStateBook().get(item->path())) {
            want = *st;
        }
    }
    const ContentXform::Value applied =
        item->hasAppliedContentXform() ? item->appliedContentXform()
                                       : ContentXform::Value{};
    SessionAppearance::mergeAppliedAndLiveFlags(
        want,
        item->hasAppliedContentXform() ? &applied : nullptr,
        item->contentHFlip(), item->contentVFlip(),
        item->sessionHasCrop(), item->sessionCropRect());
    return want;
}

DisplaySurface::State DisplayPipelineController::displaySurfaceStateForItem(const ImageItem *item,
                                                            int hostLongEdge,
                                                            bool climbPending) const
{
    return m_displayPipeline.displaySurfaceStateForItem(item, hostLongEdge, climbPending);
}


ImageItem *DisplayPipelineController::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    // Defer-populate only: size-resolve for Fill layouts must still create
    // placeholders so soft can install. applyLayout stays deferred until
    // finishGallerySizeResolve. Blocking on gallerySizeResolveActive() left
    // m_view->liveItems() empty until a manual relayout (and never for pure Fill open).
    if (m_view->hostGallerySoftBook().isDeferPopulate()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, intrinsicSize);
    m_view->applyItemModeFlags(item);
    m_view->canvasScene()->addItem(item);
    m_view->liveItems().append(item);
    registerItemDisplaySurface(item);
    return item;
}


int DisplayPipelineController::itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes) const
{
    // On-screen long edge in device pixels, snapped to a ladder step.
    // Gallery visible tiles and Image-mode zoom climb share this metric.
    if (!item) {
        return ThumtooCache::kFilmstripLadderEdge;
    }
    const QRectF br = item->contentSceneRect();
    if (br.isEmpty()) {
        return ThumtooCache::kFilmstripLadderEdge;
    }
    const QPointF a = m_view->mapFromScene(br.topLeft());
    const QPointF b = m_view->mapFromScene(br.bottomRight());
    const qreal longPx =
        ViewTransform::chebyshev(a, b) * m_view->devicePixelRatioF();
    return DisplayEdgePolicy::needEdgeFromScreenLongPx(longPx, allowHighRes);
}


int DisplayPipelineController::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    // Visible tiles: full on-screen need. Off-screen / idle: soft band only.
    return itemOnScreenNeedEdge(item, allowHighRes);
}

void DisplayPipelineController::bindImageModeSessionCursor(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Image-mode crop/flip targets the matching Workspace session slot.
    if (m_view->hostSessionId().hasCurrentId()) {
        item->setSessionId(m_view->hostSessionId().currentIdValue());
    }
    if (m_view->hostSessionId().currentIndex() >= 0) {
        item->setSessionIndex(m_view->hostSessionId().currentIndex());
    }
}


void DisplayPipelineController::resetImageModeItemPlacement(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Never inherit Gallery/Workspace free-form placement or scale.
    item->setInteractive(false);
    item->setScaleHandlesEnabled(false);
    item->setItemScale(1.0);
    item->setPos(0, 0);
    item->setItemRotation(0.0);
}


QImage DisplayPipelineController::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview) const
{
    bool unused = false;
    return resolveImageModePendingPixels(path, preview, &unused);
}


QImage DisplayPipelineController::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview,
                                                bool *displayReadyOut) const
{
    // Image-mode ←/→ hot path: process memory only (no sync thumtoo IPC).
    //
    // Soft sources, in order:
    // 1) Explicit preview / slideshow raster  → host-raw candidate
    // 2) ImageCache                          → host-raw
    // 3) Stashed Gallery tile soft:
    //    - display-ready only when same SessionImageId and applied == store want
    //      (already content-baked; must not ImageCache::put or re-materialize)
    //    - otherwise host-raw only when the tile has no content bake. Soft baked
    //      for a *different* id is skipped — not unoriented host, must not be
    //      materialize()'d again (double-bake).
    if (displayReadyOut) {
        *displayReadyOut = false;
    }
    if (!preview.isNull()) {
        return preview;
    }
    QImage pixels = m_view->hostSlideshow().slideshowRaster(path);
    // Filmstrip often has Soft while ImageCache only has size-probe LQIP (or
    // LRU-evicted the soft). Prefer strip / shared host sample first.
    if (pixels.isNull() && m_view->hostImageModeSoftProvider()) {
        bool ready = false;
        pixels = m_view->hostImageModeSoftProvider()(path, m_view->hostSessionId().currentIdValue(), &ready);
        if (!pixels.isNull()) {
            if (displayReadyOut) {
                *displayReadyOut = ready;
            }
            return pixels;
        }
    }
    // Best host in process memory (any edge). Do not require Soft ladder first —
    // filmstrip decode edge may be 128–256 and still beat LQIP.
    if (pixels.isNull()) {
        pixels = ImageCache::get(path);
    }
    // LQIP only if already in the durable/process cache — never request encode.
    if (pixels.isNull()) {
        pixels = ThumtooCache::cachedLqipImage(path);
        if (!pixels.isNull()) {
            ImageCache::put(path, pixels);
        }
    }
    if (!pixels.isNull()) {
        return pixels;
    }

    WorkspaceItemState want;
    if (m_view->hostSessionId().hasCurrentId()) {
        if (const WorkspaceItemState *st = m_view->appearance().get(m_view->hostSessionId().currentIdValue())) {
            want = *st;
        }
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);

    for (ImageItem *cand : m_gallery.stashedItems()) {
        if (!cand || cand->path() != path || !cand->hasDisplayPixels()) {
            continue;
        }
        pixels = cand->displayImage();
        if (pixels.isNull()) {
            continue;
        }
        const bool sameId = (m_view->hostSessionId().hasCurrentId()
                             && cand->sessionId() == m_view->hostSessionId().currentIdValue());
        const bool hasApplied = cand->hasAppliedContentXform();
        const ContentXform::Value applied = hasApplied
            ? cand->appliedContentXform()
            : ContentXform::Value{};
        // Display-ready: same session row and bake already matches store want.
        if (sameId && hasApplied && ContentXform::equal(applied, wantX)) {
            if (displayReadyOut) {
                *displayReadyOut = true;
            }
            return pixels;
        }
        // Host-raw: only unbaked / identity soft (safe to materialize with want).
        if (!hasApplied || ContentXform::equal(applied, ContentXform::Value{})) {
            return pixels;
        }
        // Baked for another id or mismatched want — skip.
    }
    return {};
}


SessionAppearance::PixelKind DisplayPipelineController::pixelKindForImageModeSample(
    const QString &path, const QImage &image) const
{
    // Classify by *delivered* long edge, never by the request edge.
    const int incoming = ImageCache::longEdge(image);
    if (incoming <= 0) {
        return SessionAppearance::PixelKind::SoftPreview;
    }
    // Native coverage (or PreferCache above soft max when size is provisional).
    if (sampleCoversNativeLogical(path, image)) {
        return SessionAppearance::PixelKind::FullSource;
    }
    // Soft durable ladder — SoftPreview so native / PreferCache can still upgrade.
    if (incoming <= ThumtooCache::kGalleryLadderEdge) {
        return SessionAppearance::PixelKind::SoftPreview;
    }
    // PreferCache display band without native size: FullSource for paint mode;
    // HUD uses sampleCoversNativeLogical, not hasDecodedPixels alone.
    return SessionAppearance::PixelKind::FullSource;
}


void DisplayPipelineController::applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path)
{
    if (!item || path.isEmpty()) {
        return;
    }
    // Content 90°/flip/crop are materialize()'d in createItemFromImage when want is set.
    // Legacy unbaked flips only if content flags not used yet.
    const WorkspaceItemState *st = m_view->hostItemStateBook().get(path);
    if (!st) {
        return;
    }
    if (!st->contentHFlip && !st->contentVFlip) {
        item->setItemHFlip(st->hFlip);
        item->setItemVFlip(st->vFlip);
    }
}


void DisplayPipelineController::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    if (!item) {
        return;
    }
    // Slideshow framing: when dwell motion is on, the camera sets the
    // transform (including handoff from a live transition). Applying zoom
    // framing first would centre the image then jump to motion t0.
    if (m_view->hostSlideshow().hud().isProgressActive() && m_view->hostSlideshow().settings().isMotionOff()) {
        m_view->hostSlideshow().applySlideshowZoomFraming(item);
    } else if (!m_view->hostSlideshow().hud().isProgressActive()) {
        m_view->applyImageModeFraming(item);
    }
    syncImageModeSceneRect(item);
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    m_view->hostSlideshow().maybeStartSlideshowMotion();
    if (m_view->hostSlideshow().hud().isProgressActive() && !m_view->hostSlideshow().settings().isMotionOff()
        && !m_view->hostSlideshow().dwell().isMotionActive()) {
        m_view->hostSlideshow().applySlideshowZoomFraming(item);
    }
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        m_view->hostSlideshow().setSlideshowPhase(path, QString(), -1.0);
    }
}


void DisplayPipelineController::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // ImageView::LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's ImageView::LoadAdd.
    if (!m_view->liveItems().isEmpty()
        || !m_view->hostBindBook().isEmpty()
        || loadGate().containsPendingWorkspacePath(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    item->setSelected(true);
    m_view->hostFraming().armFit();
    fitItem(item, currentFitAspectMode());
    emit statusChanged();
}



ImageItem *DisplayPipelineController::imageModeItemForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    ImageItem *cur = targetItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    cur = m_view->primaryItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    return nullptr;
}


QImage DisplayPipelineController::fullRasterForEdit(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QImage cached = ImageCache::get(path);
    if (!cached.isNull() && sampleCoversNativeLogical(path, cached)) {
        return cached;
    }
    // Never ImageLoader::load on the GUI thread — that was the crop-enter stall
    // on multi-MP files. Callers use the best available sample (cache / item)
    // and schedule requestCropFullRaster / PathRaster for a native upgrade.
    return cached;
}


int DisplayPipelineController::imageModeOnScreenNeedEdge() const
{
    if (!m_view->isImageMode()) {
        return 0;
    }
    const ImageItem *item = targetItem();
    if (!item) {
        item = m_view->primaryItem();
    }
    return itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
}


void DisplayPipelineController::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Host path→raster map: keep display-ladder samples (≤ kDisplayMaxEdge),
    // not a hard 512 preview. Slideshow must reuse Image-mode sharpness.
    if (!image.isNull() && !path.isEmpty()) {
        ImageCache::put(path, image);
        // Quality/soft climb during slideshow → phase buffer + atlas upgrade.
        if (m_view->hostSlideshow().hud().isProgressActive()) {
            m_view->hostSlideshow().onSlideshowRasterReady(path, image);
        }
    }
    switch (static_cast<LoadRole>(role)) {
    case ImageView::LoadReplace:
        completeImageView::LoadReplace(path, image, generation);
        break;
    case ImageView::LoadRestore:
        completeImageView::LoadRestore(path, image);
        break;
    case ImageView::LoadAdd:
        completeImageView::LoadAdd(path, image, generation);
        break;
    }
}



bool DisplayPipelineController::loadImage(const QString &path)
{
    m_view->setClassicPath(path);
    m_view->clearTextSelection();
    m_view->hostTextLayer().clearLinkHoverTip();
    if (m_view->hostTextLayer().showsRegions() || m_view->hostTextLayer().hasSearchQuery()) {
        m_view->refreshTextLayer();
    }
    m_view->hostSessionId().clearLastLoadError();

    if (isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_view->liveItems().isEmpty()) {
            scheduleImageLoad(path, ImageView::LoadReplace);
        }
        emit statusChanged();
        return true;
    }

    // Classic mode: soft from cache immediately; PreferCache climbs in background.
    // Do not emit statusChanged — setCurrentIndex chrome already updateStatus.
    scheduleImageLoad(path, ImageView::LoadReplace);
    return true;
}


void DisplayPipelineController::ensureImageFocusSurface()
{
    if (!m_view->isImageMode()) {
        if (imageFocusSurfaceRef() != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item || item->path().isEmpty()) {
        imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        return;
    }
    // Prefer the canvas item registry (create/destroy lifecycle).
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    // Kind may have been GalleryTile if item was created before mode switch.
    const auto itemSid =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    const DisplaySurface::Binding *ib = displaySurfaces().binding(itemSid);
    if (ib && ib->kind != DisplaySurface::Kind::ImageFocus) {
        registerItemDisplaySurface(item); // rebind as ImageFocus
    }
    imageFocusSurfaceRef() =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
}


void DisplayPipelineController::syncImageFocusSurfaceState()
{
    ensureImageFocusSurface();
    if (imageFocusSurfaceRef() == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        return;
    }
    const QString path = item->path();
    const bool pending =
        m_view->hostPathRaster() && !path.isEmpty() && m_view->hostPathRaster()->isClimbPending(path);
    syncItemDisplaySurface(item, -1, pending);
    if (m_view->hostCrop().session().isDraftSampleFrozen() && m_view->isCropDraftLockedPath(path)) {
        displaySurfaces().setFrozen(imageFocusSurfaceRef(), true);
    }
}


