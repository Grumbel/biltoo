// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop enter: bind target, install full-frame draft, activate chrome.
// docs/CROP_MODE.md / IDENTITY.md

#include "imageview.h"
#include "viewportupdatehold.h"
#include "cropflash.h"
#include "cropdebug.h"
#include "cropgeometry.h"
#include "imagecache.h"
#include "imageitem.h"
#include "contentxform.h"
#include "placementlinear.h"

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


bool ImageView::completeCropEnterUnderHold(ImageItem *item,
                                           const QPointF &workspaceAnchorScene)
{
    // One paint after full-frame draft is ready (no intermediate crop-on-old-box).
    ViewportUpdateHold paintHold(viewport());
    if (!prepareCropModeFullImage(item)) {
        // prepare may have set mode for fitItem then failed — restore placement
        // before abortEnter clears the stash.
        if (item) {
            item->setTileLodSuppressed(false);
        }
        m_crop.abortEnterRestoringPlacement(item);
        flashCropHud(CropFlash::loadFailed());
        return false;
    }
    if (isWorkspaceMode()) {
        if (item) {
            // If there was no stored crop angle but the tile was free-rotated,
            // seed the draft rotation so the frame matches the prior pose while
            // the item stays axis-aligned for editing.
            if (m_crop.seedRotationFromStashedPlacement(CropGeometry::kFreeRotationEps)) {
                ensureCropRectValid();
            }
            if (m_crop.hasValidRect()) {
                CropSession::applyScenePosDelta(
                    item,
                    PlacementLinear::scenePosDeltaToAlign(
                        item->mapToScene(m_crop.draftCenterLocal()), workspaceAnchorScene));
            }
            updateWorkspaceSceneRect();
        }
    }
    flashCropHud(CropFlash::modeEntered());
    emit cropModeChanged(true);
    emit statusChanged();
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
    // Workspace: displayed image centre so the crop frame can stay fixed.
    const QPointF workspaceAnchorScene = item->mapToScene(QPointF(0.0, 0.0));
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
    cancelPathRasterForCrop(path);
    if (!full.isNull() && sampleCoversNativeLogical(path, full)) {
        rememberSizeFromDecode(path, full);
        QSize logical = logicalSizeForPath(path);
        if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
            rememberImageSize(path, full.size());
        }
    }
    CropSession::maybePutUnorientedHostCache(
        path, full, unorientedSource, sampleCoversNativeLogical(path, full));
    const CropSession::EnterInstallSample sample =
        CropSession::prepareEnterInstallSample(full, unorientedSource, app, haveApp);
    installEnterSampleDisplay(item, sample, full, path);
}

void ImageView::installAndActivateCropEnter(ImageItem *item, const QImage &full,
                                            const WorkspaceItemState *app, bool haveApp,
                                            bool unorientedSource)
{
    // Workspace: lock scene footprint before intrinsic changes on install.
    const QRectF beforeScene =
        item ? item->mapRectToScene(item->contentRect()) : QRectF();
    installFullImageForCrop(item, full, app, haveApp, unorientedSource);
    if (item && isWorkspaceMode() && beforeScene.width() > 1.0
        && beforeScene.height() > 1.0) {
        alignItemCenterToScene(item, beforeScene.center());
    }
    m_crop.initRectFromPriorAppearance(item->contentRect(), item->offset(),
                                       item->imageSize(), app, haveApp);
    // Crop chrome + fitItem only after pixels and contentRect match the draft.
    m_crop.activateModeAfterDraft();
    fitImageOrUpdateWorkspace(item);
    m_crop.clearAwaitingFull();
}

bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    if (!item) {
        return false;
    }
    const QString path = item->path();
    m_crop.clearAwaitingFull();

    WorkspaceItemState app;
    const bool haveApp = loadRestoreCropAppearance(item, &app, nullptr);
    const bool hadCrop = CropSession::appearanceHasCrop(&app, haveApp);
    // Unoriented ImageCache host preferred. Never use item display as the crop
    // base when a prior crop exists — that bake is already cropped.
    CropSession::EnterFullRaster enter = CropSession::pickEnterFullRaster(item, path, hadCrop);
    if (enter.image.isNull()) {
        if (CropSession::shouldRequestFullOnNullEnter(hadCrop, path)) {
            requestCropFullRaster(path);
            m_crop.setAwaitingFull(path);
            flashCropHud(CropFlash::loadingFull());
        }
        flashCropHud(CropFlash::notCached());
        return false;
    }
    installAndActivateCropEnter(item, enter.image, haveApp ? &app : nullptr, haveApp,
                                enter.unoriented);
    return true;
}
