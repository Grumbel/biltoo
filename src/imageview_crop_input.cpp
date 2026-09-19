// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop input: view mapping, handle drag, rubber-band, chrome hit-test.

#include "imageview.h"
#include "cropgeometry.h"
#include "imageitem.h"

#include <QGuiApplication>

QPolygonF ImageView::mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const
{
    if (!item) {
        return {};
    }
    QPolygonF viewPoly;
    viewPoly.reserve(local.size());
    for (const QPointF &pt : local) {
        viewPoly << QPointF(mapFromScene(item->mapToScene(pt)));
    }
    return viewPoly;
}

QPolygonF ImageView::cropPolygonView() const
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropCtrl.session().hasValidRect()) {
        return {};
    }
    return mapItemLocalPolygonToView(item, m_cropCtrl.session().polygonLocal());
}

QRectF ImageView::cropRectView() const
{
    return cropPolygonView().boundingRect().normalized();
}

CropGeometry::CropButtonLayout ImageView::cropChromeLayout() const
{
    if (!m_cropCtrl.session().active()) {
        return {};
    }
    return CropGeometry::cropButtonLayout(cropRectView(),
                                          viewport() ? viewport()->rect() : QRect());
}




QPointF ImageView::itemLocalFromView(ImageItem *item, const QPoint &viewPos) const
{
    if (!item) {
        return {};
    }
    return item->mapFromScene(mapToScene(viewPos));
}

void ImageView::beginCropHandleDrag(CropHandle h, const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !CropSession::isGeometryHandle(h)) {
        return;
    }
    m_cropCtrl.session().beginHandleDrag(h, m_cropCtrl.session().currentRect(), itemLocalFromView(item, viewPos));
}


void ImageView::cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld)
{
    const Qt::KeyboardModifiers mods = QGuiApplication::keyboardModifiers();
    if (shiftHeld) {
        *shiftHeld = mods & Qt::ShiftModifier;
    }
    if (ctrlHeld) {
        *ctrlHeld = mods & Qt::ControlModifier;
    }
}

void ImageView::updateCropHandleDrag(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropCtrl.session().isHandleDragging()) {
        return;
    }
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    m_cropCtrl.session().applyActiveHandleDrag(itemLocalFromView(item, viewPos), item->contentRect(),
                                 CropSession::kMinDraftSidePx, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void ImageView::endCropHandleDrag()
{
    ImageItem *item = cropTargetItem();
    m_cropCtrl.session().finishHandleDrag(item ? item->contentRect() : QRectF());
    requestCropViewportUpdate();
}

bool ImageView::contentLocalContains(ImageItem *item, const QPointF &local) const
{
    return item && item->contentRect().contains(local);
}

void ImageView::beginCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item) {
        return;
    }
    const QPointF local = itemLocalFromView(item, viewPos);
    if (!contentLocalContains(item, local)) {
        return;
    }
    m_cropCtrl.session().beginRubberDraft(local);
    requestCropViewportUpdate();
}

void ImageView::updateCropRubberBand(const QPoint &viewPos)
{
    ImageItem *item = cropTargetItem();
    if (!item || !m_cropCtrl.session().isRubberbanding()) {
        return;
    }
    const QRectF cr = item->contentRect();
    bool shiftHeld = false;
    bool ctrlHeld = false;
    cropKeyboardMods(&shiftHeld, &ctrlHeld);
    m_cropCtrl.session().applyRubberBand(itemLocalFromView(item, viewPos), cr, shiftHeld, ctrlHeld);
    requestCropViewportUpdate();
}

void ImageView::finishCropRubberBand()
{
    if (ImageItem *item = cropTargetItem()) {
        m_cropCtrl.session().finishRubber(item->contentRect());
    } else {
        m_cropCtrl.session().endRubber();
    }
}

void ImageView::endCropRubberBand()
{
    finishCropRubberBand();
    requestCropViewportUpdate();
}

CropHandle ImageView::cropHandleAt(const QPoint &viewPos) const
{
    if (!m_cropCtrl.session().active() || !m_cropCtrl.session().hasValidRect() || !cropTargetItem()) {
        return CropHandle::None;
    }
    const CropGeometry::CropFrameViewAnchors anchors =
        CropGeometry::frameViewAnchors(cropPolygonView());
    return CropGeometry::hitTestCropChrome(viewPos, cropChromeLayout(), anchors);
}
