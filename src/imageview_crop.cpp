// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop targets, draft locks, workspace align helpers, auto-trim.
// Enter → imageview_crop_enter; Apply → crop_apply; Full → crop_raster.

#include "imageview.h"
#include "croppathraster.h"
#include "viewportupdatehold.h"
#include "cropflash.h"
#include "cropdebug.h"
#include "cropgeometry.h"
#include "placementlinear.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "sessionappearance.h"
#include "contentxform.h"

#include <QGuiApplication>
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"
#include "contentxform.h"

#include <QPainter>
#include <QtMath>
#include <QTransform>

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
    // re-resolve via selection or primaryItem() — that applied the draft to
    // unrelated tiles when selection changed mid-crop (IDENTITY.md).
    if (m_crop.active()) {
        return cropSessionBoundItem();
    }
    return resolveInactiveCropTarget();
}


void ImageView::preserveWorkspaceItemCenter(ImageItem *item, const QPointF &center0,
                                               qreal footW0, qreal footH0)
{
    // Do not rescale full frame into the previous crop footprint.
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
