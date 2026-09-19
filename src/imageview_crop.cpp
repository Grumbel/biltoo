// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop targets, draft locks, PathRaster cancel, align/leave layout, auto-trim.
// Enter → crop_enter; Apply → crop_apply; Full → crop_raster;
// paint → crop_paint; input → crop_input.

#include "imageview.h"
#include "croppathraster.h"
#include "cropflash.h"
#include "placementlinear.h"
#include "imageitem.h"

ImageItem *ImageView::cropSessionBoundItem() const
{
    // Bound subject for the active crop session (IDENTITY.md).
    if (m_crop.target()) {
        return m_crop.target();
    }
    if (m_crop.hasTargetId()) {
        return findItemBySessionId(m_crop.targetIdValue());
    }
    return nullptr;
}


ImageItem *ImageView::resolveInactiveCropTarget() const
{
    if (ImageItem *t = targetItem()) {
        // Soft ladder tiles have preview/source with m_previewPixels; pixmap is
        // often empty (paint uses m_source). hasDecodedPixels() alone was
        // "Crop / No image" for every soft-only Workspace/Gallery selection.
        if (t->hasDisplayPixels()) {
            return t;
        }
    }
    // Image mode only: sole canvas item. Gallery/Workspace need an explicit
    // single selection (hasSingleCropTarget) — never primaryItem() fan-out.
    if (isImageMode()) {
        return primaryItem();
    }
    return nullptr;
}

ImageItem *ImageView::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // retarget from selection while the draft is active.
    if (m_crop.active() || m_crop.isEnterValid()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            return bound;
        }
    }
    return resolveInactiveCropTarget();
}



bool ImageView::isCropDraftLockedItem(const ImageItem *item) const
{
    if (!item) {
        return false;
    }
    // Pointer/id lock on CropSession, then path lock (draftPath / targetId resolve).
    return m_crop.locksItem(item) || isCropDraftLockedPath(item->path());
}

bool ImageView::isCropDraftLockedPath(const QString &path) const
{
    QString boundPath;
    if (ImageItem *bound = cropSessionBoundItem()) {
        boundPath = bound->path();
    }
    return m_crop.locksResolvedPath(path, boundPath);
}

void ImageView::cancelPathRasterForCrop(const QString &path)
{
    CropPathRaster::suspend(m_pathRaster, path);
}

void ImageView::preserveWorkspaceItemCenter(ImageItem *item, const QPointF &center0,
                                            qreal footW0, qreal footH0)
{
    if (!item || !isWorkspaceMode() || footW0 <= 1.0 || footH0 <= 1.0) {
        return;
    }
    alignItemCenterToScene(item, center0);
}

void ImageView::fitImageOrUpdateWorkspace(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}

void ImageView::relayoutAfterCropLeave(ImageItem *item)
{
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else {
        fitImageOrUpdateWorkspace(item);
    }
}

void ImageView::finishCropResetLayout(ImageItem *item)
{
    if (isWorkspaceMode() && m_crop.isEnterValid()) {
        // Drop the enter-time crop-frame offset; restore pre-crop pose.
        m_crop.restoreEnterPlacementPose(item);
    }
    relayoutAfterCropLeave(item);
}

void ImageView::finishCropApplyLayout(ImageItem *item)
{
    if (!item) {
        return;
    }
    if (isWorkspaceMode()) {
        m_crop.applyCommitPlacementRotation(item);
    }
    relayoutAfterCropLeave(item);
}


void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    m_crop.ensureRectValid(item->contentRect());
}

void ImageView::alignCropFrameCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item || !m_crop.hasValidRect()) {
        return;
    }
    CropSession::applyScenePosDelta(
        item,
        PlacementLinear::scenePosDeltaToAlign(
            item->mapToScene(m_crop.draftCenterLocal()), sceneAnchor));
}

void ImageView::alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor)
{
    if (!item) {
        return;
    }
    // Pixmap is centred on the item origin (offset -w/2,-h/2).
    CropSession::applyScenePosDelta(
        item,
        PlacementLinear::scenePosDeltaToAlign(
            item->mapToScene(QPointF(0.0, 0.0)), sceneAnchor));
}

void ImageView::toggleCropMode()
{
    setCropMode(!m_crop.active());
}


void ImageView::requestCropViewportUpdate()
{
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::notifyCropViewportStatus()
{
    requestCropViewportUpdate();
    emit statusChanged();
}

void ImageView::notifyCropModeLeftChrome()
{
    emit cropModeChanged(false);
    emit statusChanged();
    if (viewport()) {
        viewport()->unsetCursor();
        viewport()->update();
    }
}

QImage ImageView::pickAutoCropSourcePixels(ImageItem *item) const
{
    return CropSession::pickAutoCropSourcePixels(item);
}

bool ImageView::runPaddedAutoTrim(ImageItem *item, const QImage &src)
{
    if (!item || src.isNull()) {
        return false;
    }
    ensureCropRectValid();
    return m_crop.tryPaddedAutoTrim(item->contentRect(), src);
}

void ImageView::applyAutoCrop()
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.active()) {
        return;
    }
    const QImage src = pickAutoCropSourcePixels(item);
    if (src.isNull()) {
        return;
    }
    if (!runPaddedAutoTrim(item, src)) {
        return;
    }
    notifyCropViewportStatus();
}

void ImageView::flashCropHud(const CropFlash::Hud &hud)
{
    flashHud(hud.title, hud.detail);
}
