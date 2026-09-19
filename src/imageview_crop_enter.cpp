// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop enter: bind target, install full-frame draft, activate chrome.
// docs/CROP_MODE.md / IDENTITY.md

#include "imageview.h"
#include "croppathraster.h"
#include "viewportupdatehold.h"
#include "cropflash.h"
#include "cropdebug.h"
#include "cropgeometry.h"
#include "placementlinear.h"
#include "imagecache.h"
#include "imageitem.h"
#include "sessionappearance.h"
#include "contentxform.h"

void ImageView::finishWorkspaceCropEnter(ImageItem *item, const QPointF &workspaceAnchorScene)
{
    if (!item) {
        return;
    }
    // If there was no stored crop angle but the tile was free-rotated,
    // seed the draft rotation so the frame matches the prior pose while
    // the item stays axis-aligned for editing.
    if (m_crop.seedRotationFromStashedPlacement(CropGeometry::kFreeRotationEps)) {
        ensureCropRectValid();
    }
    alignCropFrameCenterToScene(item, workspaceAnchorScene);
    updateWorkspaceSceneRect();
}


void ImageView::notifyCropModeEntered()
{
    flashCropHud(CropFlash::modeEntered());
    emit cropModeChanged(true);
    emit statusChanged();
}

void ImageView::abortCropEnterFailed(ImageItem *item)
{
    // prepare may have set mode for fitItem then failed — restore placement
    // before abortEnter clears the stash.
    if (item) {
        item->setTileLodSuppressed(false);
    }
    m_crop.abortEnterRestoringPlacement(item);
    flashCropHud(CropFlash::loadFailed());
}

void ImageView::beginCropEnterSession(ImageItem *item)
{
    if (!item) {
        return;
    }
    QImage enterSrc = CropSession::pickEnterSnapshotPixels(item);
    WorkspaceItemState enterSt = captureState(item);
    CropSession::seedEnterCropFlags(&enterSt, item);
    m_crop.beginEnterSession(item, enterSrc, enterSt,
                             !enterSrc.isNull() || item->hasDisplayPixels());
    cancelPathRasterForCrop(m_crop.draftPathRef());
}


bool ImageView::handleNullEnterFullRaster(const QString &path, bool hadCrop)
{
    if (CropSession::shouldRequestFullOnNullEnter(hadCrop, path)) {
        requestCropFullRaster(path);
        m_crop.setAwaitingFull(path);
        flashCropHud(CropFlash::loadingFull());
    }
    flashCropHud(CropFlash::notCached());
    return false;
}

ImageItem *ImageView::resolveCropEnterTarget()
{
    // Gallery packing cannot host crop UI — MainWindow opens Image mode instead.
    if (isGalleryMode()) {
        return nullptr;
    }
    // Image or Workspace: one explicit subject only.
    if (!hasSingleCropTarget()) {
        flashCropHud(CropFlash::needSingleTarget());
        return nullptr;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !item->hasDisplayPixels()) {
        flashCropHud(CropFlash::noImage());
        return nullptr;
    }
    return item;
}


QPointF ImageView::workspaceAnchorSceneForItem(ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Workspace: displayed image centre so the crop frame can stay fixed.
    return item->mapToScene(QPointF(0.0, 0.0));
}


bool ImageView::completeCropEnterUnderHold(ImageItem *item,
                                           const QPointF &workspaceAnchorScene)
{
    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    ViewportUpdateHold paintHold(viewport());
    if (!prepareCropModeFullImage(item)) {
        abortCropEnterFailed(item);
        return false;
    }
    if (isWorkspaceMode()) {
        finishWorkspaceCropEnter(item, workspaceAnchorScene);
    }
    notifyCropModeEntered();
    return true;
}

bool ImageView::enterCropModeFromUi()
{
    ImageItem *item = resolveCropEnterTarget();
    if (!item) {
        return false;
    }
    cancelZoomRegion();
    // Lock identity + enter snapshot + unrotate placement (IDENTITY.md).
    // m_crop.active() stays false until after the first draft attach.
    beginCropEnterSession(item);
    const QPointF workspaceAnchorScene = workspaceAnchorSceneForItem(item);
    return completeCropEnterUnderHold(item, workspaceAnchorScene);
}

void ImageView::setCropMode(bool on)
{
    if (on == m_crop.active()) {
        return;
    }
    if (on) {
        enterCropModeFromUi();
        return;
    }
    // Turning crop off from the toolbar commits the draft (auto-apply).
    leaveCropModeInternal(true);
}


bool ImageView::resolveCropEnterAppearance(ImageItem *item, WorkspaceItemState *app) const
{
    // Prior crop + content flags for this session image; path map only if unbound.
    return loadRestoreCropAppearance(item, app, nullptr);
}

void ImageView::rememberCropEnterSizes(const QString &path, const QImage &full)
{
    if (full.isNull() || !sampleCoversNativeLogical(path, full)) {
        return;
    }
    rememberSizeFromDecode(path, full);
    QSize logical = logicalSizeForPath(path);
    if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
        rememberImageSize(path, full.size());
    }
}


void ImageView::installKeepEnterDisplay(ImageItem *item, const WorkspaceItemState &contentOnly,
                                        const ContentXform::Value &wantX, const QString &path)
{
    CropSession::applyKeepEnterFlags(item, contentOnly, wantX);
    applyContentLayoutSize(item, contentOnly);
    m_crop.markShowingFullImage();
    CropDebug::keepEnterDisplay(item->displayPixelLongEdge(), path);
}


void ImageView::installDraftEnterDisplay(ImageItem *item,
                                         const CropSession::EnterInstallSample &sample,
                                         const QImage &full, const QString &path)
{
    CropSession::clearItemFreePlacementForDraft(item);
    CropDebug::draftEnterBegin(path, item->imageSize().width(), item->imageSize().height(),
                               item->hasDecodedPixels(), sample.hadPriorCrop,
                               ImageCache::longEdge(full));
    CropSession::clearItemPixelsForDraftReinstall(item);
    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;
    attachDisplaySample(item, sample.display, contentOnly, sample.kind);
    applyContentLayoutSize(item, contentOnly);
    CropSession::applyEnterDraftFlags(item, wantX);
    CropDebug::draftEnterDone(item->imageSize().width(), item->imageSize().height(),
                              sample.display.width(), sample.display.height(),
                              item->sessionHasCrop(), contentOnly.contentQuarterTurns);
    m_crop.markShowingFullImage();
}

void ImageView::prepareEnterInstallHost(const QString &path, const QImage &full,
                                        bool unorientedSource)
{
    cancelPathRasterForCrop(path);
    rememberCropEnterSizes(path, full);
    CropSession::maybePutUnorientedHostCache(
        path, full, unorientedSource, sampleCoversNativeLogical(path, full));
}


void ImageView::installEnterSampleDisplay(ImageItem *item,
                                          const CropSession::EnterInstallSample &sample,
                                          const QImage &full, const QString &path)
{
    const WorkspaceItemState &contentOnly = sample.contentOnly;
    const ContentXform::Value &wantX = sample.wantX;
    // Full-frame already on the item (no crop bake): keep those pixels.
    // Do not rebuild a lower-res graded stand-in — that invites soft↔full thrash.
    if (CropSession::canKeepDisplayForEnter(item, wantX, contentOnly, sample.hadPriorCrop,
                                            sample.needGeomBake)) {
        installKeepEnterDisplay(item, contentOnly, wantX, path);
        return;
    }
    installDraftEnterDisplay(item, sample, full, path);
}

void ImageView::installFullImageForCrop(ImageItem *item, const QImage &full,
                                        const WorkspaceItemState *app, bool haveApp,
                                        bool unorientedSource)
{
    if (!item || full.isNull()) {
        return;
    }
    const QString path = item->path();
    prepareEnterInstallHost(path, full, unorientedSource);
    const CropSession::EnterInstallSample sample =
        CropSession::prepareEnterInstallSample(full, unorientedSource, app, haveApp);
    installEnterSampleDisplay(item, sample, full, path);
}

void ImageView::activateCropModeAfterInstall(ImageItem *item)
{
    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    m_crop.activateModeAfterDraft();
    fitImageOrUpdateWorkspace(item);
    m_crop.clearAwaitingFull();
}


QRectF ImageView::captureItemContentSceneRect(ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Workspace: lock scene footprint before intrinsic changes on install.
    return item->mapRectToScene(item->contentRect());
}

void ImageView::installAndActivateCropEnter(ImageItem *item, const QImage &full,
                                            const WorkspaceItemState *app, bool haveApp,
                                            bool unorientedSource)
{
    const QRectF beforeScene = captureItemContentSceneRect(item);
    installFullImageForCrop(item, full, app, haveApp, unorientedSource);
    preserveWorkspaceItemCenter(item, beforeScene.center(),
                                beforeScene.width(), beforeScene.height());
    m_crop.initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(), app, haveApp);
    activateCropModeAfterInstall(item);
}

bool ImageView::pickEnterFullRasterOrRequest(ImageItem *item, const QString &path,
                                             bool hadCrop,
                                             CropSession::EnterFullRaster *enter)
{
    if (!enter) {
        return false;
    }
    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped.
    *enter = CropSession::pickEnterFullRaster(item, path, hadCrop);
    if (enter->image.isNull()) {
        handleNullEnterFullRaster(path, hadCrop);
        return false;
    }
    return true;
}

bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    m_crop.clearAwaitingFull();

    WorkspaceItemState app;
    const bool haveApp = resolveCropEnterAppearance(item, &app);
    const bool hadCrop = CropSession::appearanceHasCrop(&app, haveApp);
    CropSession::EnterFullRaster enter;
    if (!pickEnterFullRasterOrRequest(item, path, hadCrop, &enter)) {
        return false;
    }
    installAndActivateCropEnter(item, enter.image, haveApp ? &app : nullptr, haveApp,
                                enter.unoriented);
    return true;
}


