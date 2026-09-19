// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipelinecontroller.h"

#include "tile_load_coordinator.h"

#include "imageview.h"
#include "imageitem.h"
#include "displayedgepolicy.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"
#include "gallerysoftsm.h"
#include "displayquality.h"
#include "biltoo_thread.h"

#include "imageloader.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
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

