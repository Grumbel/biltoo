// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "crophandle.h"
#include "cropflash.h"

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QString>
#include "imageitem.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"
#include "contentxform.h"

bool ImageView::isCropDraftLockedItem(const ImageItem *item) const
{
    return m_cropCtrl.isCropDraftLockedItem(item);
}

bool ImageView::isCropDraftLockedPath(const QString &path) const
{
    return m_cropCtrl.isCropDraftLockedPath(path);
}

void ImageView::cancelPathRasterForCrop(const QString &path)
{
    m_cropCtrl.cancelPathRasterForCrop(path);
}

void ImageView::fitImageOrUpdateWorkspace(ImageItem *item)
{
    m_cropCtrl.fitImageOrUpdateWorkspace(item);
}

void ImageView::relayoutAfterCropLeave(ImageItem *item)
{
    m_cropCtrl.relayoutAfterCropLeave(item);
}

void ImageView::ensureCropRectValid()
{
    m_cropCtrl.ensureCropRectValid();
}

void ImageView::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    m_cropCtrl.alignItemCenterToScene(item, sceneAnchor);
}


void ImageView::requestCropViewportUpdate()
{
    m_cropCtrl.requestCropViewportUpdate();
}


void ImageView::flashCropHud(const CropFlash::Hud &hud)
{
    m_cropCtrl.flashCropHud(hud);
}



SessionImageId ImageView::cropRecordSessionId(const ImageItem *item) const
{
    return m_cropCtrl.cropRecordSessionId(item);
}

void ImageView::recordSessionCrop(ImageItem *item, const QRectF &localCrop)
{
    m_cropCtrl.recordSessionCrop(item, localCrop);
}

void ImageView::pushCropAppearanceUndo(ImageItem *item, const QString &text)
{
    m_cropCtrl.pushCropAppearanceUndo(item, text);
}

bool ImageView::applyCropCommit(ImageItem *item)
{
    return m_cropCtrl.applyCropCommit(item);
}

void ImageView::leaveCropModeInternal(bool apply)
{
    m_cropCtrl.leaveCropModeInternal(apply);
}



bool ImageView::prepareCropModeFullImage(ImageItem *item)
{
    return m_cropCtrl.prepareCropModeFullImage(item);
}

QRectF ImageView::cropRectView() const
{
    return m_cropCtrl.cropRectView();
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    m_cropCtrl.beginCropHandleDrag(h, viewPos);
}

void ImageView::cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld)
{
    m_cropCtrl.cropKeyboardMods(shiftHeld, ctrlHeld);
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    m_cropCtrl.updateCropHandleDrag(viewPos);
}

void ImageView::endCropHandleDrag()
{
    m_cropCtrl.endCropHandleDrag();
}

bool ImageView::contentLocalContains(ImageItem *item, const QPointF &local) const
{
    return m_cropCtrl.contentLocalContains(item, local);
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    m_cropCtrl.beginCropRubberBand(viewPos);
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    m_cropCtrl.updateCropRubberBand(viewPos);
}

void ImageView::finishCropRubberBand()
{
    m_cropCtrl.finishCropRubberBand();
}

void ImageView::endCropRubberBand()
{
    m_cropCtrl.endCropRubberBand();
}

void ImageView::paintCropOverlay(QPainter &painter)
{
    m_cropCtrl.paintCropOverlay(painter);
}

void ImageView::onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                            quint64 gen)
{
    m_cropCtrl.onPoolCropFullRasterDecoded(path, decoded, gen);
}

void ImageView::requestCropFullRaster(const QString &path)
{
    m_cropCtrl.requestCropFullRaster(path);
}

void ImageView::maybeUpgradeCropFullRaster(const QString &path, const QImage &image)
{
    m_cropCtrl.maybeUpgradeCropFullRaster(path, image);
}


ImageItem *ImageView::cropSessionBoundItem() const
{
    return m_cropCtrl.cropSessionBoundItem();
}

ImageItem *ImageView::cropTargetItem() const
{
    return m_cropCtrl.cropTargetItem();
}

// --- from src/imageview_appearance.cpp ---

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        // Dual-writes Crop (and other) sparse tables via ItemWorld.
        m_itemWorld.setAppearance(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemWorld.setPathState(item->path(), s);
    }
}


bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (sidOut) {
        *sidOut = sid;
    }
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    if (CropSession::fillAppearanceFromItemSessionCrop(app, item)) {
        return true;
    }
    if (const WorkspaceItemState *st = m_itemWorld.getPathState(item->path())) {
        *app = *st;
        return true;
    }
    return false;
}


void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    SessionImageId sid = kInvalidSessionImageId;
    if (!loadRestoreCropAppearance(item, &app, &sid)) {
        return;
    }
    const QString path = item->path();
    const QImage full = m_displayPipeline.fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
    } else if (!tryRematerializeFromHost(item, app)) {
        m_displayPipeline.installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        if (!ContentXform::equal(
                item->hasAppliedContentXform() ? item->appliedContentXform()
                                               : ContentXform::Value{},
                ContentXform::Value::fromState(app))) {
            rematerializeItemContent(item, app);
        }
    }
    fitImageOrUpdateWorkspace(item);
}


void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    if (!src.isNull()) {
        attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
    } else {
        item->setSessionCrop(state.hasCrop, state.cropRect);
        item->setContentHFlip(state.contentHFlip);
        item->setContentVFlip(state.contentVFlip);
        item->setAppliedContentXform(ContentXform::Value::fromState(state));
    }
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }
        WorkspaceItemState slot = state;
        slot.sessionId = sid;
        slot.path = item->path();
        storeCropAppearance(item, sid, slot);
    }
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}


void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                           ImageItem *item, const QImage &preferredDisplay,
                                           bool hasCrop)
{
    if (sid == kInvalidSessionImageId) {
        return;
    }
    // Prefer the crop bake just materialized when provided — not displayImage(),
    // which can still be pre-crop if soft attach was rejected.
    QImage appearance = preferredDisplay;
    if (appearance.isNull() && item) {
        appearance = sessionAppearanceImage(item);
    }
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit sessionAppearanceChanged(sid, path, appearance);
    }
    emit sessionCropApplied(sid, path, appearance, hasCrop);
}

