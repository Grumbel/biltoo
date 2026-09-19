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

void ImageView::toggleCropMode()
{
    m_cropCtrl.toggleCropMode();
}

void ImageView::requestCropViewportUpdate()
{
    m_cropCtrl.requestCropViewportUpdate();
}

void ImageView::applyAutoCrop()
{
    m_cropCtrl.applyAutoCrop();
}

void ImageView::flashCropHud(const CropFlash::Hud &hud)
{
    m_cropCtrl.flashCropHud(hud);
}

void ImageView::applyCrop()
{
    m_cropCtrl.applyCrop();
}

void ImageView::cancelCrop()
{
    m_cropCtrl.cancelCrop();
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

bool ImageView::enterCropModeFromUi()
{
    return m_cropCtrl.enterCropModeFromUi();
}

void ImageView::setCropMode(bool on)
{
    m_cropCtrl.setCropMode(on);
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
