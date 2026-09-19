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
    if (m_cropCtrl.session().target()) {
        return m_cropCtrl.session().target();
    }
    if (m_cropCtrl.session().hasTargetId()) {
        return findItemBySessionId(m_cropCtrl.session().targetIdValue());
    }
    return nullptr;
}


ImageItem *ImageView::cropTargetItem() const
{
    // Crop session is bound to one subject for its entire lifetime. Never
    // retarget from selection while the draft is active.
    if (m_cropCtrl.session().active() || m_cropCtrl.session().isEnterValid()) {
        if (ImageItem *bound = cropSessionBoundItem()) {
            return bound;
        }
    }
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



bool ImageView::isCropDraftLockedItem(const ImageItem *item) const
{
    if (!item) {
        return false;
    }
    // Pointer/id lock on CropSession, then path lock (draftPath / targetId resolve).
    return m_cropCtrl.session().locksItem(item) || isCropDraftLockedPath(item->path());
}

bool ImageView::isCropDraftLockedPath(const QString &path) const
{
    QString boundPath;
    if (ImageItem *bound = cropSessionBoundItem()) {
        boundPath = bound->path();
    }
    return m_cropCtrl.session().locksResolvedPath(path, boundPath);
}

void ImageView::cancelPathRasterForCrop(const QString &path)
{
    CropPathRaster::suspend(m_pathRaster, path);
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

void ImageView::ensureCropRectValid()
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    m_cropCtrl.session().ensureRectValid(item->contentRect());
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
    setCropMode(!m_cropCtrl.session().active());
}


void ImageView::requestCropViewportUpdate()
{
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::applyAutoCrop()
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropCtrl.session().active()) {
        return;
    }
    const QImage src = CropSession::pickAutoCropSourcePixels(item);
    if (src.isNull()) {
        return;
    }
    ensureCropRectValid();
    if (!m_cropCtrl.session().tryPaddedAutoTrim(item->contentRect(), src)) {
        return;
    }
    requestCropViewportUpdate();
    emit statusChanged();
}

void ImageView::flashCropHud(const CropFlash::Hud &hud)
{
    flashHud(hud.title, hud.detail);
}
