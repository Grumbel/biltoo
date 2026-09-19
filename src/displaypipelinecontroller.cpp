// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipelinecontroller.h"

#include "tile_load_coordinator.h"

#include "imageview.h"
#include "imageitem.h"
#include "displayedgepolicy.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"
#include "biltoo_thread.h"

#include "imageloader.h"
#include "imagecache.h"
#include "contentxform.h"
#include "imagesizebook.h"
#include "biltoo_logging.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>

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


