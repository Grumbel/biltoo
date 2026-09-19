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
#include "biltoo_logging.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>
#include <cstdlib>
#include <cstdio>

#include <QGraphicsScene>
#include <QGraphicsItem>

DisplayPipelineController::DisplayPipelineController(ImageView *view)
    : m_view(view)
{
}

void DisplayPipelineController::ensureWorkspaceQualityClimb()
{
    ASSERT_GUI_THREAD();
    if (!m_view->isWorkspaceMode() || !m_view->m_pathRaster || !m_view->m_scene) {
        return;
    }
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : m_view->m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            targets.append(ii);
        }
    }
    if (targets.isEmpty()) {
        // No selection: climb all on-canvas items (bounded).
        int n = 0;
        for (ImageItem *ii : m_view->m_items) {
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
        const bool pending = m_view->m_pathRaster->isClimbPending(path);
        m_view->syncItemDisplaySurface(ii, -1, pending);
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
            && (m_view->m_pathRaster->isGaveUp(path)
                || (!m_view->m_pathRaster->isClimbPending(path)
                    && act.type == DisplaySurface::ActionType::ScheduleClimb))) {
            // PreferCache plateau short of need — native only when no durable tiles.
            if (!ThumtooCache::hasDurableTilesKnown(path)
                && (m_view->m_pathRaster->isGaveUp(path)
                    || !m_view->m_pathRaster->isClimbPending(path))) {
                m_view->scheduleImageModeNativeDecodeOnce(path);
            }
        }
    }
}

void DisplayPipelineController::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty() || m_view->m_gallerySoftBook.hasImageModeNativeDecode(path)) {
        return;
    }
    m_view->m_gallerySoftBook.markImageModeNativeDecode(path);
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
    if (!m_view->m_pathRaster || path.isEmpty() || m_view->m_slideshow.hud().isNavHot()) {
        return;
    }
    if (m_view->isCropDraftLockedPath(path)) {
        return;
    }
    // Tiles own display: tileLodWanted or known durable pyramid — no PreferCache.
    if (ImageItem *it = m_view->imageModeItemForPath(path)) {
        if (it->tileLodWanted() || ThumtooCache::hasDurableTilesKnown(path)) {
            m_view->tickPrimaryTileLod(12);
            return;
        }
    }
    for (ImageItem *ii : m_view->m_items) {
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
        (m_view->m_slideshow.hud().isProgressActive() && ThumtooCache::hasDurableTilesKnown(path))
            ? PathRasterService::ClimbPolicy::SoftDisplay
            : PathRasterService::ClimbPolicy::EscalateToFull;
    biltooLoadDbg("escalateClimb(service) path=%s edge=%d policy=%d",
                  qPrintable(QFileInfo(path).fileName()), edge,
                  static_cast<int>(policy));
    m_view->m_pathRaster->ensure(path, edge, m_view->logicalSizeForPath(path), policy);
}
void DisplayPipelineController::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    if (m_view->m_pathRaster && !path.isEmpty()) {
        m_view->m_pathRaster->noteDelivery(path, requestEdge, sample);
    }
}
void DisplayPipelineController::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    if (path.isEmpty() || m_view->m_slideshow.hud().isNavHot() || !m_view->m_pathRaster) {
        return;
    }
    if (m_view->isCropDraftLockedPath(path)) {
        return;
    }
    if (m_view->m_slideshow.hud().isProgressActive()) {
        return;
    }
    // Tiles own display once wanted or durable pyramid is known — no PreferCache.
    const bool durable = ThumtooCache::hasDurableTilesKnown(path);
    if (ImageItem *it = m_view->imageModeItemForPath(path)) {
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
    m_view->m_pathRaster->ensure(path, climbTo, m_view->logicalSizeForPath(path),
                         PathRasterService::ClimbPolicy::EscalateToFull);
    // Full is async. If terminal or nothing pending and still short, host native
    // (only when no durable tiles — prepared libs use TileSynth / tile LOD).
    if (!ThumtooCache::hasDurableTilesKnown(path)
        && !DisplayEdgePolicy::coversEdge(m_view->m_pathRaster->haveEdge(path), climbTo)
        && (m_view->m_pathRaster->isGaveUp(path) || !m_view->m_pathRaster->isClimbPending(path))) {
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
    m_view->installDisplayPixels(item, image, kind, item->sessionId() != kInvalidSessionImageId
                                             ? item->sessionId()
                                             : m_view->m_sessionId.currentIdValue());
    m_view->m_sessionId.clearLastLoadError();
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
    if (ImageItem *cur = m_view->imageModeItemForPath(path)) {
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
            if (!(noUpgrade && m_view->m_pathRaster && m_view->m_pathRaster->isGaveUp(path))) {
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
    // FullSource-only createItemFromImage is not forced on a soft sample.
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
    if (m_view->m_pathRaster) {
        m_view->m_pathRaster->noteDelivery(path, maxEdge, image);
        // PreferCache BestAvailable / Full escalate is PathRasterService policy
        // (docs/THUMTOO_HOST_CONTRACT.md). Do not recover ad-hoc here.
    } else {
        if (!image.isNull()) {
            ImageCache::put(path, image);
        }
        if (m_view->m_slideshow.hud().isProgressActive() && !image.isNull()) {
            m_view->m_slideshow.onSlideshowRasterReady(path, image);
        }
    }

    // Image mode: soft→sharp when full ImageLoader::load missed or is still
    // in flight. ladderReady used to return early for non-Gallery, so PDF /
    // page / archive PreferCache deliveries left the view stuck on the soft
    // thumbnail forever.
    if (m_view->isImageMode() && !image.isNull() && !m_view->m_slideshow.hud().isProgressActive()) {
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
        for (ImageItem *ii : m_view->m_items) {
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
        for (ImageItem *item : m_view->m_items) {
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

    if (GallerySoftState *st = m_view->m_gallerySoftBook.find(path)) {
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
    if (!m_view->isImageMode() || m_view->m_slideshow.hud().isProgressActive()) {
        return;
    }
    ImageItem *item = m_view->imageModeItemForPath(m_view->classicPath());
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
        if (m_view->isGalleryMode() || !m_view->m_pathRaster) {
            return false;
        }
        // Image key-repeat: never ensure per skipped path (IMAGE_MODE_NAV_SOFT).
        if (m_view->m_slideshow.hud().isNavHot() && m_view->isImageMode()) {
            return false;
        }
        const int need = act.climbNeedEdge > 0 ? act.climbNeedEdge : fallbackNeedEdge;
        if (need > 0) {
            m_view->m_pathRaster->ensure(path, need, m_view->logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::ScheduleAsyncMaterialize) {
        if (m_view->isGalleryMode()) {
            return false;
        }
        if (m_view->m_slideshow.hud().isNavHot() && m_view->isImageMode()) {
            return false;
        }
        m_view->scheduleAsyncHostRematerialize(
            path, item->sessionId(),
            m_view->wantAppearanceForItem(item, item->sessionId()));
        // Intermediate Prefer host is baking async; keep Soft→Prefer→Full climb
        // when on-screen need is still above host (Workspace zoom-in).
        const int need = fallbackNeedEdge > 0 ? fallbackNeedEdge : 0;
        if (m_view->m_pathRaster && need > 0
            && !DisplayEdgePolicy::coversEdge(item->displayPixelLongEdge(), need)
            && !DisplayEdgePolicy::coversEdge(ImageCache::longEdge(ImageCache::get(path)), need)) {
            m_view->m_pathRaster->ensure(path, need, m_view->logicalSizeForPath(path), climbPolicy);
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
            if (m_view->m_scene) {
                m_view->m_scene->update(item->sceneBoundingRect());
            }
        }
        const bool sizeChanged = (item->imageSize() != before);
        if (act.type == AT::AttachSoft) {
            syncItemDisplaySurface(item, ImageCache::longEdge(host),
                m_view->m_pathRaster && m_view->m_pathRaster->isClimbPending(path));
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
            } else if (again.type == AT::ScheduleClimb && m_view->m_pathRaster
                       && !m_view->isGalleryMode()) {
                const int need = again.climbNeedEdge > 0 ? again.climbNeedEdge
                                                         : fallbackNeedEdge;
                if (need > 0) {
                    m_view->m_pathRaster->ensure(path, need, m_view->logicalSizeForPath(path),
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
    if (!m_view->isImageMode() || m_view->m_slideshow.hud().isProgressActive()) {
        return;
    }
    // Key-repeat: install soft only. evaluate() → ScheduleClimb / async bake
    // would pathRaster->ensure every skipped path (bypassed requestEscalateClimb
    // nav-hot guard). Settle loadImage drives the surface once.
    if (m_view->m_slideshow.hud().isNavHot()) {
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
                                                          : m_view->m_sessionId.currentIdValue();
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
        } else if (m_view->isImageMode() && m_sessionId.hasCurrentId()) {
            sid = m_sessionId.currentIdValue();
        }
    }
    seedSessionAppearanceFromState(sid, path);

    // Absolute want xform (session store / path map / live flags).
    const WorkspaceItemState appearance = m_view->wantAppearanceForItem(item, sid);

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
    if (m_view->isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
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
        const bool navHot = m_view->m_slideshow.hud().isNavHot() && m_view->isImageMode();
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
    } else if (m_view->isImageMode() && m_view->m_scene && m_view->m_items.size() == 1) {
        m_view->m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
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
    // Slideshow owns the viewport with dwell/live blits. Pending tile used to
    // clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed. Underlay is hidden for the whole show.
    if (m_view->m_slideshow.hud().isProgressActive()) {
        return;
    }
    if (isCropDraftLockedPath(path)) {
        return;
    }

    bool displayReady = false;
    QImage pixels = m_view->resolveImageModePendingPixels(path, preview, &displayReady);
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
        if (m_view->m_items.size() == 1) {
            ImageItem *item = m_view->m_items.first();
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
                    syncImageModeSceneRect(item);
                }
                // Soft PreferCache encode is removed for Image underlay
                // (LQIP + tiles only). Nav-hot: no IPC — settle loadImage probes
                // size and issues tiles once. Do not scheduleSoftPixels here.
                if (!m_view->m_slideshow.hud().isNavHot() && ThumtooCache::isAvailable()) {
                    ThumtooCache::scheduleProbe(path);
                }
                if (m_view->viewport()) {
                    m_view->viewport()->update();
                }
                biltooLoadDbg(
                    m_view->m_slideshow.hud().isNavHot()
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
    if (m_view->m_items.size() == 1) {
        item = m_view->m_items.first();
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
        // Path change: drop prior sample AND content chrome. wantAppearanceForItem
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
        const WorkspaceItemState want = wantAppearanceForItem(item, item->sessionId());
        if (displayReady && SessionAppearance::hasContentAppearance(want)) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                pixels = host;
                displayReady = false;
            }
        }
        if (displayReady) {
            attachDisplaySample(item, pixels, want,
                                SessionAppearance::PixelKind::SoftPreview);
        } else {
            installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }

        // Intrinsic only from definitive file size — never from soft/LQIP sz.
        const QSize known = logicalSizeForPath(path);
        QSize targetSize = item->imageSize();
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !isProvisionalImageSize(path)) {
            targetSize = ContentXform::layoutSize(known, want);
        }
        int didFit = 0;
        if (isPositiveSize(targetSize) && targetSize.width() > 1
            && !isProvisionalImageSize(path)) {
            item->setIntrinsicSize(targetSize);
            const bool needFit =
                sizeBefore.width() <= 1
                || ContentXform::aspectChanged(sizeBefore, targetSize);
            if (needFit || m_view->m_framing.isStickyZoomEnabled() || m_view->m_framing.hasPreservedViewScale()) {
                // Aspect change, sticky mode, or free-zoom preserve across files.
                m_view->resetImageModeItemPlacement(item);
                applyImageModeFraming(item);
                didFit = 1;
            } else if (sizeBefore != targetSize) {
                preserveImageViewOnLogicalSizeChange(item, sizeBefore, targetSize);
            }
        }
        // Single press: sync repaint so soft is visible before PreferCache.
        // Key-repeat (nav hot): async update only — sync repaint every auto-repeat
        // event was the cumulative GUI freeze under held ←/→.
        if (m_view->viewport()) {
            if (m_view->m_slideshow.hud().isNavHot()) {
                m_view->viewport()->update();
            } else {
                m_view->viewport()->repaint();
            }
        }
        // Retained path RAM: bind session and paint tiles without waiting for
        // the next coordinator timer (A→B→A should show tiles on this frame).
        if (!m_view->m_slideshow.hud().isNavHot() && item->tileLodHasPathRam()) {
            item->tickTileLod(8);
        }
        biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d fit=%d painted=%s",
                      qPrintable(QFileInfo(path).fileName()),
                      pixels.width(), pixels.height(), didFit,
                      m_view->m_slideshow.hud().isNavHot() ? "async" : "sync");
        return;
    }

    // No reusable item — still try to capture from whatever was on the canvas.
    if (!m_view->m_items.isEmpty()) {
        m_view->captureStickyPanAnchor(m_view->m_items.first());
    }
    m_view->clearLiveCanvas();
    item = m_view->createPlaceholderItem(path, sz);
    if (!item) {
        m_view->setUpdatesEnabled(true);
        return;
    }
    m_view->bindImageModeSessionCursor(item);
    installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                         m_view->m_sessionId.currentIdValue());
    m_view->resetImageModeItemPlacement(item);
    m_view->prepareImageModeCanvas();
    applyImageModeFraming(item);
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
    if (!m_view->m_items.isEmpty()) {
        if (m_view->m_items.first()->path() != path) {
            m_view->captureStickyPanAnchor(m_view->m_items.first());
        } else if (m_view->m_framing.isStickyZoomEnabled()
                   && !m_view->m_framing.isStickyFit()) {
            // Same path rebuild: keep looking where we are now.
            m_view->captureStickyPanAnchor(m_view->m_items.first());
        }
    }
    // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
    m_view->clearLiveCanvas();
    ImageItem *item = m_view->createItemFromImage(path, image);
    if (!item) {
        m_view->setUpdatesEnabled(true);
        m_view->m_sessionId.setLastLoadError(path);
        emit m_view->statusChanged();
        return;
    }
    // Filmstrip overrides are not driven by decode (selection/nav).
    // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
    // Arbitrary Workspace rotation stays on the free-form item only.
    // createItemFromImage materializes host × store want (install invariant).
    m_view->bindImageModeSessionCursor(item);
    m_view->resetImageModeItemPlacement(item);
    m_view->applyLegacyPathFlipsIfNeeded(item, path);
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
    // Empty multi-item canvas can still seed from classicPath.
    if (path != m_view->classicPath()) {
        return;
    }
    if (image.isNull()) {
        if (ThumtooCache::isAvailable()) {
            // Full native miss: PreferCache display ladder so onLadderReady can
            // upgrade Image mode (soft→HQ). Skip when tiles already own zoom.
            bool tilesOwn = false;
            if (ImageItem *it = m_view->imageModeItemForPath(path)) {
                tilesOwn = it->tileLodWanted();
            }
            if (!tilesOwn) {
                scheduleImageModePreferCacheClimb(path, ThumtooCache::kBatchOverviewEdge);
            }
            m_view->m_sessionId.clearLastLoadError();
        } else {
            m_view->m_sessionId.setLastLoadError(path);
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
        m_view->m_sessionId.clearLastLoadError();
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return;
    }
    qWarning("ImageView: decode failed for %s", qPrintable(path));
    if (m_view->isGalleryMode()) {
        GallerySoftState &st = m_view->m_gallerySoftBook.state(path);
        st.failed = true;
        st.inflight = 0;
    }
    m_view->m_sessionId.setLastLoadError(path);
    // Surface the error on any live placeholder for this path.
    for (ImageItem *item : m_view->m_items) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            item->setToolTip(m_view->tr("Failed to load:\n%1").arg(path));
        }
    }
    finishLoadAddStatus(/*refreshGalleryWindow=*/true);
}


void DisplayPipelineController::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    for (ImageItem *cand : m_view->m_gallery.stashedItems()) {
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
    for (ImageItem *item : m_view->m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        for (int bi = 0; bi < m_view->m_bindBook.bindCount(); ++bi) {
            const PendingSessionBind &b = m_view->m_bindBook.bindAt(bi);
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
    for (ImageItem *existing : m_view->m_items) {
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
        if (bound.id != kInvalidSessionImageId && appearance().get(bound.id)) {
            applyState(existing, *appearance().get(bound.id));
        }
        // Explicit drop position wins over restored gallery/workspace pose.
        applyPendingBindScenePos(existing, bound);
        if (bound.id != kInvalidSessionImageId) {
            // Decode must not rewrite filmstrip (sessionAppearanceChanged).
            if (m_view->m_bindBook.removeSelectId(bound.id)) {
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
    for (ImageItem *existing : m_view->m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        // Soft was wrongly stored as "decoded"; still accept stricter long edge.
        if (!existing->hasDecodedPixels()
            || existing->shouldUpgradeDisplayTo(incoming)) {
            if (installFullPreservingWorkspaceFootprint(existing, image)) {
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
    if (gallerySizeResolveActive() || m_view->m_gallerySoftBook.isDeferPopulate()) {
        return;
    }
    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {
        ImageItem *item = m_view->createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        PendingSessionBind bound;
        const bool haveBound = m_view->takePendingSessionBindForNewItem(path, item, &bound);
        applyStoredAppearance(item);
        // Decode/membership must not rewrite filmstrip; user edits emit overrides.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            // Paste: select tiles as they finish decoding.
            if (m_view->m_bindBook.removeSelectId(bound.id)) {
                item->setSelected(true);
            }
        }
        m_view->placeNewLoadAddItem(item, path, image, haveBound, bound);
    }
}


void DisplayPipelineController::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    if (gallerySizeResolveActive() || m_view->m_gallerySoftBook.isDeferPopulate()) {
        return;
    }
    if (!m_layout.isFreeForm()) {
        if (!pathOrderIsEmpty()) {
            reorderItemsByPaths(pathOrderPaths());
        }
        if (!(m_view->isGalleryMode() && m_galleryRelayoutSuppress.active())) {
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


