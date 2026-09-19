// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop Apply / leave commit: bake, record, undo, rematerialize flush.

#include "imageview.h"
#include "cropappearancecommand.h"
#include "viewportupdatehold.h"
#include "cropflash.h"
#include "cropdebug.h"
#include "imageitem.h"
#include "sessionappearance.h"

#include <QUndoStack>

bool ImageView::resolveApplyHostAndState(ImageItem *item, QImage *host, bool *hostFromCache,
                                         WorkspaceItemState *st, SessionImageId *sid)
{
    if (!item || !host || !hostFromCache || !st || !sid) {
        return false;
    }
    const QString path = item->path();
    *host = CropSession::pickApplyHost(item, path, hostFromCache);
    const CropSession::ApplyHostStatus hostSt =
        CropSession::classifyApplyHost(*host, *hostFromCache, item);
    if (flashApplyHostFailure(hostSt)) {
        return false;
    }
    *sid = cropRecordSessionId(item);
    ensureApplyCropState(item, *sid, st);
    return true;
}

void ImageView::captureApplyDraftMetrics(ImageItem *item, qreal *cropW, qreal *cropH,
                                         qreal *footW, qreal *footH, QPointF *sceneCenter)
{
    qreal sx0 = 0.0;
    qreal sy0 = 0.0;
    CropSession::itemScalePair(item, &sx0, &sy0);
    m_crop.draftFootprint(sx0, sy0, cropW, cropH, footW, footH);
    if (sceneCenter) {
        *sceneCenter = item->mapToScene(m_crop.draftCenterLocal());
    }
}

bool ImageView::materializeApplyBake(const QImage &host, bool hostFromCache,
                                     const WorkspaceItemState &st,
                                     CropSession::ApplyBakeResult *baked)
{
    if (!baked) {
        return false;
    }
    *baked = CropSession::materializeApplyDisplay(host, hostFromCache, st);
    if (!baked->ok()) {
        flashCropHud(CropFlash::bakeFailed());
        return false;
    }
    return true;
}

void ImageView::finalizeCropResetSuccess(ImageItem *item)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(cropRecordSessionId(item), item->path(), item, QImage(),
                            /*hasCrop=*/false);
    if (m_crop.shouldPushResetUndo(item->sourceImage().size())) {
        pushCropAppearanceUndo(item, CropFlash::undoResetText());
    }
    flashCropHud(CropFlash::reset());
}

void ImageView::finalizeCropApplySuccess(ImageItem *item, SessionImageId sid,
                                         const QString &path, const QImage &display)
{
    commitItemSessionEdit(item);
    emitCropApplyAppearance(sid, path, item, display, /*hasCrop=*/true);
    pushCropAppearanceUndo(item, CropFlash::undoCropText());
    flashCropHud(CropFlash::applied(item->imageSize().width(), item->imageSize().height()));
}



void ImageView::applyCrop()
{
    // Soft draft is valid — crop is content-space. Do not wait on multi-MP load.
    m_crop.clearAwaitingFull();
    leaveCropModeInternal(true);
}

void ImageView::cancelCrop()
{
    leaveCropModeInternal(false);
}

SessionImageId ImageView::cropRecordSessionId(const ImageItem *item) const
{
    return CropSession::sessionIdForRecord(
        item,
        m_crop.hasTargetId() ? m_crop.targetIdValue() : kInvalidSessionImageId,
        isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
}


void ImageView::writeRecordedCropState(ImageItem *item, SessionImageId sid,
                                       const WorkspaceItemState *orientApp,
                                       const CropSession::RecordGeometry &rec,
                                       const QSize &cropBasis, const QRect &disp)
{
    WorkspaceItemState s = captureState(item);
    CropSession::mergeOrientFromAppearance(&s, orientApp);
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    m_crop.applyRecordToState(&s, rec, cropBasis);
    CropDebug::recordCrop(cropBasis, item->imageSize(), disp);
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    storeCropAppearance(item, sid, s);
}


void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    if (!item) {
        return;
    }
    // Crop mode always edits the full on-disk image — store absolute source rect.
    // Map through active flips so cropRect is in unflipped source space
    // (cropToLocalRect bakes flips into pixels and clears the flags).
    CropSession::RecordGeometry rec = m_crop.computeRecordGeometry(
        localCrop, item->contentRect(), item->offset(),
        item->imageSize().width(), item->imageSize().height(),
        item->itemHFlip(), item->itemVFlip());
    if (!rec.valid()) {
        return;
    }
    const SessionImageId sid = cropRecordSessionId(item);
    const WorkspaceItemState *orientApp =
        (sid != kInvalidSessionImageId) ? m_appearance.get(sid) : nullptr;
    const QSize cropBasis = CropSession::cropBasisSize(
        item->imageSize(), cropRecordFileNative(item->path()), orientApp, item);
    writeRecordedCropState(item, sid, orientApp, rec, cropBasis, rec.sourceRect);
}


WorkspaceItemState ImageView::captureCropUndoAfterState(ImageItem *item) const
{
    WorkspaceItemState afterSt = captureState(item);
    CropSession::fillSessionCropFromItem(&afterSt, item);
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    return afterSt;
}

void ImageView::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    if (!m_undoStack || !item || !m_crop.isEnterValid()) {
        return;
    }
    m_undoStack->push(new CropAppearanceCommand(
        this, item, m_crop.enterSourceRef(), item->sourceImage().copy(),
        m_crop.enterStateRef(), captureCropUndoAfterState(item), text));
}


void ImageView::attachCropApplyDisplay(ImageItem *item, const QImage &display,
                                          const WorkspaceItemState &st, bool multiMp,
                                          qreal cropW, qreal cropH, const QString &path,
                                          const QPointF &cropSceneCenter)
{
    // Clear first so the crop bake replaces full-frame pixels — otherwise canvas
    // stretches full into the crop box and filmstrip gets img=full.
    item->clearDecodedPixels();
    // Geometry before pixels: empty item with crop intrinsic, then bake.
    applyContentLayoutSize(item, st);
    CropSession::ensureApplyIntrinsicSize(item, cropW, cropH, path);
    attachDisplaySample(item, display, st, CropSession::applyPixelKind(multiMp));
    m_crop.restoreEnterScale(item);
    alignItemCenterToScene(item, cropSceneCenter);
}


bool ImageView::flashApplyHostFailure(CropSession::ApplyHostStatus hostSt)
{
    if (hostSt == CropSession::ApplyHostStatus::Ok) {
        return false;
    }
    flashCropHud(CropFlash::applyHostStatus(hostSt));
    return true;
}

void ImageView::ensureApplyCropState(ImageItem *item, SessionImageId sid,
                                     WorkspaceItemState *st)
{
    if (!item || !st) {
        return;
    }
    loadSessionAppearance(sid, st);
    if (!st->hasCrop) {
        *st = captureState(item);
        m_crop.seedApplyCropState(st, item->offset(), item->imageSize());
        if (sid != kInvalidSessionImageId) {
            m_appearance.set(sid, *st);
        }
    }
}

void ImageView::commitCropApplyBake(ImageItem *item, const QImage &display,
                                    const WorkspaceItemState &st, bool multiMp,
                                    qreal cropW, qreal cropH, const QString &path,
                                    const QPointF &cropSceneCenter, bool hostFromCache,
                                    SessionImageId sid)
{
    {
        ViewportUpdateHold paintHold(viewport());
        attachCropApplyDisplay(item, display, st, multiMp, cropW, cropH, path,
                               cropSceneCenter);
        // Multi-MP: soft stand-in now; pure full rematerialize after leave.
        m_crop.queueFullRematerializeIfSoft(hostFromCache, multiMp, path, sid, st);
        finishCropApplyLayout(item);
    }
    finalizeCropApplySuccess(item, sid, path, display);
}


bool ImageView::bakeAndCommitNonFullApply(ImageItem *item, qreal cropW, qreal cropH,
                                          qreal footW, qreal footH,
                                          const QPointF &cropSceneCenter)
{
    const QString path = item->path();
    bool hostFromCache = false;
    QImage host;
    WorkspaceItemState st;
    SessionImageId sid = kInvalidSessionImageId;
    if (!resolveApplyHostAndState(item, &host, &hostFromCache, &st, &sid)) {
        return false;
    }

    CropSession::ApplyBakeResult baked;
    if (!materializeApplyBake(host, hostFromCache, st, &baked)) {
        return false;
    }
    CropDebug::applyCrop(path, host.width(), host.height(), hostFromCache,
                         baked.display.width(), baked.display.height(), cropW, cropH, footW,
                         footH, item->imageSize().width(), item->imageSize().height());
    commitCropApplyBake(item, baked.display, st, baked.multiMp, cropW, cropH, path,
                        cropSceneCenter, hostFromCache, sid);
    return true;
}

bool ImageView::applyCropCommitNonFullFrame(ImageItem *item)
{
    // Workspace footprint: draft selection scene size stays constant after Apply
    // (intrinsic becomes cropW×cropH at the same placement scale).
    qreal cropW = 0.0;
    qreal cropH = 0.0;
    qreal footW = 0.0;
    qreal footH = 0.0;
    QPointF cropSceneCenter;
    captureApplyDraftMetrics(item, &cropW, &cropH, &footW, &footH, &cropSceneCenter);
    if (!bakeAndCommitNonFullApply(item, cropW, cropH, footW, footH, cropSceneCenter)) {
        return false;
    }
    return isWorkspaceMode();
}


bool ImageView::applyCropCommitFullFrame(ImageItem *item)
{
    // Reset / full frame: keep full pixels; clear session crop metadata.
    finishCropResetLayout(item);
    finalizeCropResetSuccess(item);
    return false;
}

bool ImageView::applyCropCommit(ImageItem *item)
{
    // Returns true when Workspace placement rotation should keep the crop-frame
    // angle (non-full-frame commit).
    ensureCropRectValid();
    const QRectF full = item->contentRect();
    recordSessionCrop(item, m_crop.draftRectOr(full));
    if (!m_crop.isFullFrameDraft(full)) {
        return applyCropCommitNonFullFrame(item);
    }
    return applyCropCommitFullFrame(item);
}

void ImageView::cancelCropShowingFullImage(ImageItem *item)
{
    restoreSessionCropAppearance(item);
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        m_crop.restoreEnterPlacementPose(item);
    }
}


void ImageView::flushPendingFullRematerialize(bool pendingFull, const QString &pendingPath,
                                              SessionImageId pendingSid,
                                              const WorkspaceItemState &pendingWant)
{
    // Apply may have queued a full bake while freeze was still on.
    if (pendingFull && !pendingPath.isEmpty()) {
        scheduleAsyncHostRematerialize(pendingPath, pendingSid, pendingWant);
    }
}


void ImageView::clearCropModeState()
{
    // Stop PreferCache before releasing tile LOD / clearing draft identity.
    QString subjectPath = m_crop.draftPathRef();
    if (subjectPath.isEmpty()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            subjectPath = bound->path();
        }
    }
    cancelPathRasterForCrop(subjectPath);
    m_crop.releaseAllTileLod(cropSessionBoundItem());
    QString pendingPath;
    SessionImageId pendingSid = kInvalidSessionImageId;
    WorkspaceItemState pendingWant;
    const bool pendingFull =
        m_crop.takePendingFullRematerialize(&pendingPath, &pendingSid, &pendingWant);
    m_crop.clear();
    notifyCropModeLeftChrome();
    flushPendingFullRematerialize(pendingFull, pendingPath, pendingSid, pendingWant);
}


bool ImageView::finalizeCropLeaveItem(ImageItem *item, bool apply)
{
    if (!item) {
        return false;
    }
    if (apply) {
        return applyCropCommit(item);
    }
    if (m_crop.isShowingFullImage()) {
        cancelCropShowingFullImage(item);
    }
    return false;
}

void ImageView::leaveCropModeInternal(bool apply)
{
    if (!m_crop.active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    // Workspace commit keeps crop-frame placement rotation; cancel/full-frame
    // restore the pre-crop pose via finishLeave.
    const bool preserveCropFrameRotation = finalizeCropLeaveItem(item, apply);
    m_crop.finishLeave(item, preserveCropFrameRotation);
    clearCropModeState();
}
