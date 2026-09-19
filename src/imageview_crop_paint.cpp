// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop chrome / overlay paint (HANDLES.md). Geometry in CropGeometry.

#include "imageview.h"
#include "cropgeometry.h"
#include "imageitem.h"

#include <QPainter>

void ImageView::paintCropChromeButton(QPainter &painter, const QRect &btn, CropHandle kind,
                                      const QString &label, CropGeometry::CropBtnRole role,
                                      bool toggled)
{
    CropGeometry::paintTextButton(painter, btn, m_crop.currentHoverHandle() == kind, label,
                                  role, toggled);
}

QString ImageView::cropChromeButtonLabel(CropHandle kind)
{
    switch (kind) {
    case CropHandle::ExpandToggle:
        return tr("Expand");
    case CropHandle::Auto:
        return tr("Auto");
    case CropHandle::Reset:
        return tr("Reset");
    case CropHandle::Cancel:
        return tr("Cancel");
    case CropHandle::Close:
        return tr("Apply");
    default:
        return {};
    }
}

void ImageView::paintCropChromeButtons(QPainter &painter)
{
    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md).
    const CropGeometry::CropButtonLayout chrome = cropChromeLayout();
    for (const CropGeometry::ChromePaintItem &item :
         CropGeometry::chromePaintItems(chrome, m_crop.isAllowExpand())) {
        if (item.rect.isEmpty()) {
            continue;
        }
        paintCropChromeButton(painter, item.rect, item.handle,
                              cropChromeButtonLabel(item.handle), item.role,
                              item.toggled);
    }
}



void ImageView::paintCropRotateAndMoveGrips(QPainter &painter, const QPolygonF &cropViewPoly)
{
    const bool rotateHot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                            || m_crop.currentActiveHandle() == CropHandle::Rotate);
    CropGeometry::paintRotateKnobs(painter, cropViewPoly, rotateHot);
    const bool moveHot = (m_crop.currentHoverHandle() == CropHandle::Move
                          || m_crop.currentActiveHandle() == CropHandle::Move);
    CropGeometry::paintMoveGrip(painter, cropViewPoly, moveHot);
}

void ImageView::paintCropFrameDecorations(QPainter &painter, const QPolygonF &cropViewPoly)
{
    if (viewport()) {
        CropGeometry::paintDimOutside(painter, viewport()->rect(), cropViewPoly);
    }
    CropGeometry::paintFrame(painter, cropViewPoly);
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return m_crop.isHandleHot(h); });
    paintCropRotateAndMoveGrips(painter, cropViewPoly);
}

void ImageView::paintCropSizeBadge(QPainter &painter, const QRect &cropView)
{
    const QSize cropSz = m_crop.draftPixelSize();
    CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());
}


void ImageView::paintCropOverlayBody(QPainter &painter, const QPolygonF &cropViewPoly,
                                     const QRect &cropView)
{
    paintCropFrameDecorations(painter, cropViewPoly);
    paintCropChromeButtons(painter);
    paintCropSizeBadge(painter, cropView);
}

void ImageView::paintCropOverlay(QPainter &painter)
{
    if (!m_crop.active()) {
        return;
    }
    ImageItem *item = cropTargetItem();
    if (!item || !m_crop.hasValidRect()) {
        return;
    }
    ensureCropRectValid();
    const QPolygonF cropViewPoly = cropPolygonView();
    const QRect cropView = cropViewPoly.boundingRect().toRect().normalized();

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintCropOverlayBody(painter, cropViewPoly, cropView);
    painter.restore();
}

