// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView → DisplayPipelineController forwards (Tier 5).
// Real load/tile/soft host logic stays in imageview_load.cpp.

#include "imageview.h"
#include "thumtoocache.h"
#include "sessionappearance.h"

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
    m_displayPipeline.bindImageModeSessionCursor(item);
}

void ImageView::resetImageModeItemPlacement(ImageItem *item)
{
    m_displayPipeline.resetImageModeItemPlacement(item);
}

QImage ImageView::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview) const
{
    return m_displayPipeline.resolveImageModePendingPixels(path, preview);
}

QImage ImageView::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview,
                                                bool *displayReadyOut) const
{
    return m_displayPipeline.resolveImageModePendingPixels(path, preview, displayReadyOut);
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
    return m_displayPipeline.pixelKindForImageModeSample(path, image);
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
    m_displayPipeline.applyLegacyPathFlipsIfNeeded(item, path);
}

void ImageView::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    m_displayPipeline.frameImageModeReplaceItem(item, path);
}

void ImageView::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    m_displayPipeline.installImageModeReplaceItem(path, image);
}

void ImageView::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    m_displayPipeline.seedEmptyWorkspaceFromReplace(path, image);
}

ImageItem *ImageView::imageModeItemForPath(const QString &path) const
{
    return m_displayPipeline.imageModeItemForPath(path);
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
    return m_displayPipeline.fullRasterForEdit(path);
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
    return m_displayPipeline.imageModeOnScreenNeedEdge();
}

void ImageView::scheduleTileLodAfterInteraction(int delayMs)
{
    m_displayPipeline.scheduleTileLodAfterInteraction(delayMs);
}

void ImageView::purgeTilePathRam(const QString &path)
{
    m_displayPipeline.purgeTilePathRam(path);
}

void ImageView::dropAllTileLodSessions()
{
    m_displayPipeline.dropAllTileLodSessions();
}

void ImageView::tickPrimaryTileLod(int budget)
{
    m_displayPipeline.tickPrimaryTileLod(budget);
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
    m_displayPipeline.onImageLoaded(path, image, generation, role);
}

bool ImageView::loadImage(const QString &path)
{
    return m_displayPipeline.loadImage(path);
}

void ImageView::ensureImageFocusSurface()
{
    m_displayPipeline.ensureImageFocusSurface();
}

void ImageView::syncImageFocusSurfaceState()
{
    m_displayPipeline.syncImageFocusSurfaceState();
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

