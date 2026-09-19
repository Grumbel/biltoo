// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "tilelod/tile_lod_registry.hpp"
#include "gallerysoftsm.h"
#include "viewtransform.h"
#include "displayedgepolicy.h"
#include "softdisplaypolicy.h"
#include <QVarLengthArray>
#include "ttfp_trace.h"

#include <algorithm>
#include "displayquality.h"
#include "coloradjust.h"

#include "archivepath.h"
#include "imagecache.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "sessionappearance.h"
#include "contentxform.h"
#include "thumtoocache.h"
#if __has_include("thumtoo/client.hpp")
#include "thumtoo/client.hpp"
#endif
#include "biltoo_thread.h"
#include "biltoo_logging.h"
#include "tile_load_coordinator.h"
#include "tilelod/tile_lod_controller.hpp"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QtMath>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace {

/**
 * Worker-side: bake durable content appearance and clamp for display install.
 * Must not run on the GUI — materialize + scale of multi-MP samples is the
 * ←/→ hitch when done in installDisplayPixels.
 */
// prepareImageModeDisplaySample removed: workers must return host-raw only.
// GUI installDisplayPixels is the sole materialize site (see CONTENT_PIPELINE
// install invariant). Baking in the worker + put/materialize on the GUI
// double-applied crop and polluted ImageCache with content-baked samples.

} // namespace

int ImageView::pathOrderOccurrences(const QString &path) const
{
    // View-local multiplicity only (Gallery pack / LoadAdd).
    // clearPathOrder() zeros the book without wiping SessionDocument — consulting
    // the document here would recreate session tiles on a blank Workspace.
    return m_pathOrderBook.countPathOccurrences(path);
}

WorkspaceItemState ImageView::appearanceForNewImageModeItem(const QString &path)
{
    return m_displayPipeline.appearanceForNewImageModeItem(path);
}

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    return m_displayPipeline.createItemFromImage(path, image, applyStoredSessionCrop);
}

void ImageView::seedSessionAppearancesFromPaths(const QStringList &paths,
                                                   const QVector<SessionImageId> &ids)
{
    m_displayPipeline.seedSessionAppearancesFromPaths(paths, ids);
}

void ImageView::seedSessionAppearanceFromState(SessionImageId sid, const QString &path)
{
    m_displayPipeline.seedSessionAppearanceFromState(sid, path);
}

void ImageView::markAppearanceSeedAttempted(SessionImageId sid)
{
    m_displayPipeline.markAppearanceSeedAttempted(sid);
}

void ImageView::applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                                 const ThumtooCache::StoredContentAppearance &stored)
{
    m_displayPipeline.applyStoredContentAppearanceSeed(sid, path, stored);
}

void ImageView::installDisplayPreservingView(ImageItem *item, const QImage &pixels,
                                             SessionAppearance::PixelKind kind,
                                             SessionImageId sid)
{
    m_displayPipeline.installDisplayPreservingView(item, pixels, kind, sid);
}

WorkspaceItemState ImageView::wantAppearanceForItem(const ImageItem *item,
                                                      SessionImageId sid) const
{
    return m_displayPipeline.wantAppearanceForItem(item, sid);
}

bool ImageView::applyDisplaySurfaceAction(ImageItem *item,
                                          const DisplaySurface::Action &act,
                                          const QImage &hostSample,
                                          int fallbackNeedEdge,
                                          PathRasterService::ClimbPolicy climbPolicy)
{
    return m_displayPipeline.applyDisplaySurfaceAction(item, act, hostSample, fallbackNeedEdge, climbPolicy);
}

bool ImageView::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    return m_displayPipeline.canAcceptDisplaySample(item, pixels, kind);
}

void ImageView::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    m_displayPipeline.installDisplayPixels(item, pixels, kind, sid);
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    return m_displayPipeline.createPlaceholderItem(path, intrinsicSize);
}

void ImageView::bindImageModeSessionCursor(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Image-mode crop/flip targets the matching Workspace session slot.
    if (m_sessionId.hasCurrentId()) {
        item->setSessionId(m_sessionId.currentIdValue());
    }
    if (m_sessionId.currentIndex() >= 0) {
        item->setSessionIndex(m_sessionId.currentIndex());
    }
}

void ImageView::resetImageModeItemPlacement(ImageItem *item)
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

QImage ImageView::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview) const
{
    bool unused = false;
    return resolveImageModePendingPixels(path, preview, &unused);
}

QImage ImageView::resolveImageModePendingPixels(const QString &path,
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
    QImage pixels = m_slideshow.slideshowRaster(path);
    // Filmstrip often has Soft while ImageCache only has size-probe LQIP (or
    // LRU-evicted the soft). Prefer strip / shared host sample first.
    if (pixels.isNull() && m_imageModeSoftProvider) {
        bool ready = false;
        pixels = m_imageModeSoftProvider(path, m_sessionId.currentIdValue(), &ready);
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
    if (m_sessionId.hasCurrentId()) {
        if (const WorkspaceItemState *st = appearance().get(m_sessionId.currentIdValue())) {
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
        const bool sameId = (m_sessionId.hasCurrentId()
                             && cand->sessionId() == m_sessionId.currentIdValue());
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

void ImageView::setImageModeSoftProvider(ImageModeSoftProvider provider)
{
    m_imageModeSoftProvider = std::move(provider);
}

void ImageView::installImageModePendingTile(const QString &path, const QImage &preview)
{
    m_displayPipeline.installImageModePendingTile(path, preview);
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    m_displayPipeline.scheduleImageLoad(path, static_cast<int>(role));
}

bool ImageView::tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen)
{
    return m_displayPipeline.tryDeliverReplaceFromSlideshowRaster(path, gen);
}

void ImageView::scheduleSlideshowReplaceDecode(const QString &path, quint64 gen,
                                               LoadRole role)
{
    m_displayPipeline.scheduleSlideshowReplaceDecode(path, gen, static_cast<int>(role));
}

void ImageView::scheduleClassicImageDecode(const QString &path, quint64 gen,
                                           LoadRole role)
{
    m_displayPipeline.scheduleClassicImageDecode(path, gen, static_cast<int>(role));
}

int ImageView::itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes) const
{
    return m_displayPipeline.itemOnScreenNeedEdge(item, allowHighRes);
}

int ImageView::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    return m_displayPipeline.galleryDisplayEdgeForItem(item, allowHighRes);
}

void ImageView::gallerySoftResetPath(const QString &path)
{
    m_displayPipeline.gallerySoftResetPath(path);
}

void ImageView::gallerySoftResetAll()
{
    m_displayPipeline.gallerySoftResetAll();
}

int ImageView::galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const
{
    return m_displayPipeline.galleryHaveEdgeFromItems(path, anyFullOut);
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    m_displayPipeline.scheduleGalleryDecode(path);
}

void ImageView::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    m_displayPipeline.onLadderReady(path, maxEdge, image);
}

SessionAppearance::PixelKind ImageView::pixelKindForImageModeSample(
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

void ImageView::upgradeImageModeFromLadder(const QString &path, int maxEdge,
                                           const QImage &image)
{
    m_displayPipeline.upgradeImageModeFromLadder(path, maxEdge, image);
}

void ImageView::applyGalleryLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    m_displayPipeline.applyGalleryLadderReady(path, maxEdge, image);
}

void ImageView::applyWorkspaceLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    m_displayPipeline.applyWorkspaceLadderReady(path, maxEdge, image);
}

void ImageView::ensureWorkspaceQualityClimb()
{
    m_displayPipeline.ensureWorkspaceQualityClimb();
}


void ImageView::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    m_displayPipeline.scheduleImageModeNativeDecodeOnce(path);
}


void ImageView::onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                                     int role)
{
    m_displayPipeline.onImagePreviewLoaded(path, image, generation, role);
}

bool ImageView::takePendingRestoreState(const QString &path, WorkspaceItemState *out)
{
    return m_displayPipeline.takePendingRestoreState(path, out);
}

void ImageView::completeLoadRestore(const QString &path, const QImage &image)
{
    m_displayPipeline.completeLoadRestore(path, image);
}

void ImageView::finishLoadAddStatus(bool refreshGalleryWindow)
{
    m_displayPipeline.finishLoadAddStatus(refreshGalleryWindow);
}

bool ImageView::acceptPendingLoadAdd(const QString &path, quint64 generation)
{
    return m_displayPipeline.acceptPendingLoadAdd(path, generation);
}

void ImageView::handleLoadAddDecodeFailure(const QString &path)
{
    m_displayPipeline.handleLoadAddDecodeFailure(path);
}

void ImageView::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    m_displayPipeline.fillStashedItemsForPath(path, image);
}

void ImageView::reassertPendingBindPlacement(const QString &path)
{
    m_displayPipeline.reassertPendingBindPlacement(path);
}

void ImageView::claimUnboundItemsForPendingBinds(const QString &path, const QImage &image)
{
    m_displayPipeline.claimUnboundItemsForPendingBinds(path, image);
}

int ImageView::fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                              bool *sizeChangedOut)
{
    return m_displayPipeline.fillLiveItemsWithDecodedPixels(path, image, sizeChangedOut);
}

void ImageView::createMissingLoadAddItems(const QString &path, const QImage &image,
                                          int have, int wanted)
{
    m_displayPipeline.createMissingLoadAddItems(path, image, have, wanted);
}

void ImageView::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    m_displayPipeline.applyLoadAddLayoutAfterMembership(sizeChanged);
}

void ImageView::completeLoadAdd(const QString &path, const QImage &image, quint64 generation)
{
    m_displayPipeline.completeLoadAdd(path, image, generation);
}

void ImageView::applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path)
{
    if (!item || path.isEmpty()) {
        return;
    }
    // Content 90°/flip/crop are materialize()'d in createItemFromImage when want is set.
    // Legacy unbaked flips only if content flags not used yet.
    const WorkspaceItemState *st = m_itemStateBook.get(path);
    if (!st) {
        return;
    }
    if (!st->contentHFlip && !st->contentVFlip) {
        item->setItemHFlip(st->hFlip);
        item->setItemVFlip(st->vFlip);
    }
}

void ImageView::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    if (!item) {
        return;
    }
    // Slideshow framing: when dwell motion is on, the camera sets the
    // transform (including handoff from a live transition). Applying zoom
    // framing first would centre the image then jump to motion t0.
    if (m_slideshow.hud().isProgressActive() && m_slideshow.settings().isMotionOff()) {
        m_slideshow.applySlideshowZoomFraming(item);
    } else if (!m_slideshow.hud().isProgressActive()) {
        applyImageModeFraming(item);
    }
    syncImageModeSceneRect(item);
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    m_slideshow.maybeStartSlideshowMotion();
    if (m_slideshow.hud().isProgressActive() && !m_slideshow.settings().isMotionOff()
        && !m_slideshow.dwell().isMotionActive()) {
        m_slideshow.applySlideshowZoomFraming(item);
    }
    if (m_slideshow.hud().isProgressActive()) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        m_slideshow.setSlideshowPhase(path, QString(), -1.0);
    }
}

void ImageView::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    m_displayPipeline.installImageModeReplaceItem(path, image);
}

void ImageView::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's LoadAdd.
    if (!m_items.isEmpty()
        || !m_bindBook.isEmpty()
        || m_displayPipeline.loadGate().containsPendingWorkspacePath(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    item->setSelected(true);
    m_framing.armFit();
    fitItem(item, currentFitAspectMode());
    emit statusChanged();
}


ImageItem *ImageView::imageModeItemForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    ImageItem *cur = targetItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    cur = primaryItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    return nullptr;
}

void ImageView::scheduleImageModePreferCacheClimb(const QString &path, int wantEdge)
{
    m_displayPipeline.scheduleImageModePreferCacheClimb(path, wantEdge);
}


void ImageView::requestEscalateClimb(const QString &path, int wantEdge)
{
    m_displayPipeline.requestEscalateClimb(path, wantEdge);
}


void ImageView::installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                             const QImage &image,
                                             SessionAppearance::PixelKind kind)
{
    m_displayPipeline.installImageModeSampleInPlace(item, path, image, kind);
}

int ImageView::cappedDisplayEdgeForPath(const QString &path, int wantEdge) const
{
    return m_displayPipeline.cappedDisplayEdgeForPath(path, wantEdge);
}

QImage ImageView::fullRasterForEdit(const QString &path) const
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

bool ImageView::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    return m_displayPipeline.sampleCoversNativeLogical(path, image);
}

void ImageView::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    m_displayPipeline.noteImageModePreferCacheDelivery(path, requestEdge, sample);
}


void ImageView::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    m_displayPipeline.ensureImageModeQualityClimb(path, sample);
}


bool ImageView::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    return m_displayPipeline.tryInstallImageModeSample(path, image);
}

bool ImageView::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    return m_displayPipeline.tryInstallImageModeSampleBaked(path, image, kind);
}

int ImageView::imageModeOnScreenNeedEdge() const
{
    if (!isImageMode()) {
        return 0;
    }
    const ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    return itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
}

void ImageView::scheduleTileLodAfterInteraction(int delayMs)
{
    if (!m_displayPipeline.tileLodZoomDebounce()) {
        m_displayPipeline.tileLodZoomDebounce() = new QTimer(this);
        m_displayPipeline.tileLodZoomDebounce()->setSingleShot(true);
        connect(m_displayPipeline.tileLodZoomDebounce(), &QTimer::timeout, this, [this]() {
            if (isGalleryMode()) {
                tickPrimaryTileLod(8);
                return;
            }
            if (isImageMode()) {
                maybeClimbImageModePixelsForView();
            } else if (isWorkspaceMode()) {
                ensureWorkspaceQualityClimb();
            }
        });
    }
    m_displayPipeline.tileLodZoomDebounce()->setInterval(ViewTransform::nonNegMs(delayMs));
    m_displayPipeline.tileLodZoomDebounce()->start();
}

void ImageView::prefetchTilesForPaths(const QStringList &paths, int budgetPerPath)
{
    m_tileNeighborPrefetch.prefetchPaths(paths, budgetPerPath);
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    for (const ImageItem *ii : m_items) {
        if (ii && ii->path() == path) {
            return true;
        }
    }
    return false;
}

QSize ImageView::viewportWidgetSize() const
{
    if (const QWidget *vp = viewport()) {
        return vp->size();
    }
    return {};
}

qreal ImageView::prefetchDevicePixelRatio() const
{
    if (const QWidget *vp = viewport()) {
        return vp->devicePixelRatioF();
    }
    return 1.0;
}

bool ImageView::tilePrefetchNavHot() const
{
    return m_slideshow.hud().isNavHot();
}


void ImageView::dropTilePrefetchPath(const QString &path)
{
    m_tileNeighborPrefetch.dropPath(path);
}

void ImageView::purgeTilePathRam(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    for (ImageItem *item : m_items) {
        if (item && item->path() == path) {
            item->dropTileLodSession();
        }
    }
    tilelod::TileLodRegistry::instance().invalidate(path);
    dropTilePrefetchPath(path);
}

void ImageView::dropAllTileLodSessions()
{
    auto dropList = [](const QList<ImageItem *> &items) {
        for (ImageItem *item : items) {
            if (item) {
                item->dropTileLodSession();
            }
        }
    };
    dropList(m_items);
    dropList(m_gallery.stashedItems());
    dropList(m_workspace.stashedItems());
}

void ImageView::tickPrimaryTileLod(int budget)
{
    ASSERT_GUI_THREAD();
    // Key-repeat: do not plan/issue tiles — soft underlay only until settle.
    if (m_slideshow.hud().isNavHot()) {
        return;
    }
    if (!m_displayPipeline.tileCoordinator()) {
        m_displayPipeline.tileCoordinator() = std::make_unique<TileLoadCoordinator>(this);
    }
    m_displayPipeline.tileCoordinator()->tick(budget);

    // Image/Workspace: keep issuing until every tileLodWanted item is covered.
    // Without a re-arm, only the first budget of center keys climbed to target
    // scale; outer cells stayed one level coarse until a scroll forced a tick.
    bool needMore = false;
    for (ImageItem *ii : m_items) {
        if (!ii || !ii->tileLodWanted()) {
            continue;
        }
        if (!ii->tileLodViewportCovered()) {
            needMore = true;
            break;
        }
    }
    if (!needMore) {
        return;
    }
    if (!m_displayPipeline.tileLodTimer()) {
        m_displayPipeline.tileLodTimer() = new QTimer(this);
        m_displayPipeline.tileLodTimer()->setSingleShot(true);
        connect(m_displayPipeline.tileLodTimer(), &QTimer::timeout, this, [this]() {
            // Image focus: higher budget so density climb is not starved.
            tickPrimaryTileLod(isGalleryMode() ? 48 : 32);
        });
    }
    if (!m_displayPipeline.tileLodTimer()->isActive()) {
        m_displayPipeline.tileLodTimer()->start(16);
    }
}


void ImageView::maybeClimbImageModePixelsForView()
{
    m_displayPipeline.maybeClimbImageModePixelsForView();
}

void ImageView::completeLoadReplace(const QString &path, const QImage &image, quint64 generation)
{
    m_displayPipeline.completeLoadReplace(path, image, generation);
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Host path→raster map: keep display-ladder samples (≤ kDisplayMaxEdge),
    // not a hard 512 preview. Slideshow must reuse Image-mode sharpness.
    if (!image.isNull() && !path.isEmpty()) {
        ImageCache::put(path, image);
        // Quality/soft climb during slideshow → phase buffer + atlas upgrade.
        if (m_slideshow.hud().isProgressActive()) {
            m_slideshow.onSlideshowRasterReady(path, image);
        }
    }
    switch (static_cast<LoadRole>(role)) {
    case LoadReplace:
        completeLoadReplace(path, image, generation);
        break;
    case LoadRestore:
        completeLoadRestore(path, image);
        break;
    case LoadAdd:
        completeLoadAdd(path, image, generation);
        break;
    }
}


bool ImageView::loadImage(const QString &path)
{
    setClassicPath(path);
    clearTextSelection();
    m_textLayer.clearLinkHoverTip();
    if (m_textLayer.showsRegions() || m_textLayer.hasSearchQuery()) {
        refreshTextLayer();
    }
    m_sessionId.clearLastLoadError();

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

    // Classic mode: soft from cache immediately; PreferCache climbs in background.
    // Do not emit statusChanged — setCurrentIndex chrome already updateStatus.
    scheduleImageLoad(path, LoadReplace);
    return true;
}

void ImageView::ensureImageFocusSurface()
{
    if (!isImageMode()) {
        if (m_displayPipeline.imageFocusSurfaceRef() != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            m_displayPipeline.imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = primaryItem();
    if (!item || item->path().isEmpty()) {
        m_displayPipeline.imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        return;
    }
    // Prefer the canvas item registry (create/destroy lifecycle).
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    // Kind may have been GalleryTile if item was created before mode switch.
    const auto itemSid =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    const DisplaySurface::Binding *ib = m_displayPipeline.displaySurfaces().binding(itemSid);
    if (ib && ib->kind != DisplaySurface::Kind::ImageFocus) {
        registerItemDisplaySurface(item); // rebind as ImageFocus
    }
    m_displayPipeline.imageFocusSurfaceRef() =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
}

void ImageView::syncImageFocusSurfaceState()
{
    ensureImageFocusSurface();
    if (m_displayPipeline.imageFocusSurfaceRef() == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    ImageItem *item = primaryItem();
    if (!item) {
        return;
    }
    const QString path = item->path();
    const bool pending =
        m_pathRaster && !path.isEmpty() && m_pathRaster->isClimbPending(path);
    syncItemDisplaySurface(item, -1, pending);
    if (m_cropCtrl.session().isDraftSampleFrozen() && isCropDraftLockedPath(path)) {
        m_displayPipeline.displaySurfaces().setFrozen(m_displayPipeline.imageFocusSurfaceRef(), true);
    }
}

void ImageView::driveImageFocusSurface()
{
    m_displayPipeline.driveImageFocusSurface();
}

void ImageView::registerItemDisplaySurface(ImageItem *item)
{
    m_displayPipeline.registerItemDisplaySurface(item);
}

void ImageView::unregisterItemDisplaySurface(ImageItem *item)
{
    m_displayPipeline.unregisterItemDisplaySurface(item);
}

void ImageView::syncItemDisplaySurface(ImageItem *item, int hostLongEdge,
                                       bool climbPending)
{
    m_displayPipeline.syncItemDisplaySurface(item, hostLongEdge, climbPending);
}

