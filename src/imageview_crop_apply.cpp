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
    QSize fileNative = logicalSizeForPath(item->path());
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1
        || isProvisionalImageSize(item->path())) {
        fileNative = {};
    }
    const QSize cropBasis = CropSession::cropBasisSize(
        item->imageSize(), fileNative, orientApp, item);
    WorkspaceItemState s = captureState(item);
    CropSession::mergeOrientFromAppearance(&s, orientApp);
    s.sessionId = sid;
    s.sessionIndex = item->sessionIndex();
    m_crop.applyRecordToState(&s, rec, cropBasis);
    CropDebug::recordCrop(cropBasis, item->imageSize(), rec.sourceRect);
    s.path = item->path();
    item->setSessionCrop(s.hasCrop, s.cropRect);
    storeCropAppearance(item, sid, s);
}


void ImageView::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    if (!m_undoStack || !item || !m_crop.isEnterValid()) {
        return;
    }
    WorkspaceItemState afterSt = captureState(item);
    CropSession::fillSessionCropFromItem(&afterSt, item);
    // captureState pulls cropRotation from appearance
    // (recordSessionCrop + commitItemSessionEdit).
    m_undoStack->push(new CropAppearanceCommand(
        this, item, m_crop.enterSourceRef(), item->sourceImage().copy(),
        m_crop.enterStateRef(), afterSt, text));
}


bool ImageView::applyCropCommit(ImageItem *item)
{
    // Returns true when Workspace placement rotation should keep the crop-frame
    // angle (non-full-frame commit).
    ensureCropRectValid();
    const QRectF full = item->contentRect();
    recordSessionCrop(item, m_crop.draftRectOr(full));
    if (!m_crop.isFullFrameDraft(full)) {
        // Workspace footprint: draft selection scene size stays constant after Apply
        // (intrinsic becomes cropW×cropH at the same placement scale).
        qreal cropW = 0.0;
        qreal cropH = 0.0;
        qreal footW = 0.0;
        qreal footH = 0.0;
        QPointF cropSceneCenter;
        qreal sx0 = 0.0;
        qreal sy0 = 0.0;
        CropSession::itemScalePair(item, &sx0, &sy0);
        m_crop.draftFootprint(sx0, sy0, &cropW, &cropH, &footW, &footH);
        cropSceneCenter = item->mapToScene(m_crop.draftCenterLocal());

        const QString path = item->path();
        bool hostFromCache = false;
        QImage host = CropSession::pickApplyHost(item, path, &hostFromCache);
        const CropSession::ApplyHostStatus hostSt =
            CropSession::classifyApplyHost(host, hostFromCache, item);
        if (hostSt != CropSession::ApplyHostStatus::Ok) {
            flashCropHud(CropFlash::applyHostStatus(hostSt));
            return false;
        }
        SessionImageId sid = cropRecordSessionId(item);
        WorkspaceItemState st;
        loadSessionAppearance(sid, &st);
        if (!st.hasCrop) {
            st = captureState(item);
            m_crop.seedApplyCropState(&st, item->offset(), item->imageSize());
            if (sid != kInvalidSessionImageId) {
                m_appearance.set(sid, st);
            }
        }
        CropSession::ApplyBakeResult baked =
            CropSession::materializeApplyDisplay(host, hostFromCache, st);
        if (!baked.ok()) {
            flashCropHud(CropFlash::bakeFailed());
            return false;
        }
        CropDebug::applyCrop(path, host.width(), host.height(), hostFromCache,
                             baked.display.width(), baked.display.height(), cropW, cropH, footW,
                             footH, item->imageSize().width(), item->imageSize().height());
        {
            ViewportUpdateHold paintHold(viewport());
            // Clear first so the crop bake replaces full-frame pixels — otherwise canvas
            // stretches full into the crop box and filmstrip gets img=full.
            item->clearDecodedPixels();
            // Geometry before pixels: empty item with crop intrinsic, then bake.
            applyContentLayoutSize(item, st);
            CropSession::ensureApplyIntrinsicSize(item, cropW, cropH, path);
            attachDisplaySample(item, baked.display, st,
                                CropSession::applyPixelKind(baked.multiMp));
            m_crop.restoreEnterScale(item);
            alignItemCenterToScene(item, cropSceneCenter);
            // Multi-MP: soft stand-in now; pure full rematerialize after leave.
            m_crop.queueFullRematerializeIfSoft(hostFromCache, baked.multiMp, path, sid, st);
            if (isWorkspaceMode()) {
                m_crop.applyCommitPlacementRotation(item);
            }
            relayoutAfterCropLeave(item);
        }
        commitItemSessionEdit(item);
        emitCropApplyAppearance(sid, path, item, baked.display, /*hasCrop=*/true);
        pushCropAppearanceUndo(item, CropFlash::undoCropText());
        flashCropHud(CropFlash::applied(item->imageSize().width(), item->imageSize().height()));
        return isWorkspaceMode();
    }
    // Reset / full frame: keep full pixels; clear session crop metadata.
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        m_crop.restoreEnterPlacementPose(item);
    }
    relayoutAfterCropLeave(item);
    commitItemSessionEdit(item);
    emitCropApplyAppearance(cropRecordSessionId(item), item->path(), item, QImage(),
                            /*hasCrop=*/false);
    if (m_crop.shouldPushResetUndo(item->sourceImage().size())) {
        pushCropAppearanceUndo(item, CropFlash::undoResetText());
    }
    flashCropHud(CropFlash::reset());
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
    bool preserveCropFrameRotation = false;
    if (item) {
        if (apply) {
            preserveCropFrameRotation = applyCropCommit(item);
        } else if (m_crop.isShowingFullImage()) {
            restoreSessionCropAppearance(item);
            if (isWorkspaceMode() && m_crop.isEnterValid()) {
                m_crop.restoreEnterPlacementPose(item);
            }
        }
    }
    m_crop.finishLeave(item, preserveCropFrameRotation);
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
    emit cropModeChanged(false);
    emit statusChanged();
    if (viewport()) {
        viewport()->unsetCursor();
        viewport()->update();
    }
    // Apply may have queued a full bake while freeze was still on.
    if (pendingFull && !pendingPath.isEmpty()) {
        scheduleAsyncHostRematerialize(pendingPath, pendingSid, pendingWant);
    }
}
