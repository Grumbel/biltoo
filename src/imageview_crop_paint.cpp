// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Crop chrome / overlay paint (HANDLES.md). Geometry in CropGeometry.

#include "imageview.h"
#include "cropgeometry.h"
#include "imageitem.h"

#include <QPainter>

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

    if (viewport()) {
        CropGeometry::paintDimOutside(painter, viewport()->rect(), cropViewPoly);
    }
    CropGeometry::paintFrame(painter, cropViewPoly);
    CropGeometry::paintResizeHandles(painter, cropViewPoly,
        [this](CropHandle h) { return m_crop.isHandleHot(h); });

    const bool rotateHot = (m_crop.currentHoverHandle() == CropHandle::Rotate
                            || m_crop.currentActiveHandle() == CropHandle::Rotate);
    CropGeometry::paintRotateKnobs(painter, cropViewPoly, rotateHot);
    const bool moveHot = (m_crop.currentHoverHandle() == CropHandle::Move
                          || m_crop.currentActiveHandle() == CropHandle::Move);
    CropGeometry::paintMoveGrip(painter, cropViewPoly, moveHot);

    // Controls: outside below crop when possible, inside if off-screen.
    // Same design language as Workspace chrome (HANDLES.md).
    const CropGeometry::CropButtonLayout chrome = cropChromeLayout();
    for (const CropGeometry::ChromePaintItem &chromeItem :
         CropGeometry::chromePaintItems(chrome, m_crop.isAllowExpand())) {
        if (chromeItem.rect.isEmpty()) {
            continue;
        }
        QString label;
        switch (chromeItem.handle) {
        case CropHandle::ExpandToggle:
            label = tr("Expand");
            break;
        case CropHandle::Auto:
            label = tr("Auto");
            break;
        case CropHandle::Reset:
            label = tr("Reset");
            break;
        case CropHandle::Cancel:
            label = tr("Cancel");
            break;
        case CropHandle::Close:
            label = tr("Apply");
            break;
        default:
            break;
        }
        CropGeometry::paintTextButton(
            painter, chromeItem.rect,
            m_crop.currentHoverHandle() == chromeItem.handle, label, chromeItem.role,
            chromeItem.toggled);
    }

    const QSize cropSz = m_crop.draftPixelSize();
    CropGeometry::paintSizeBadge(painter, cropView, cropSz.width(), cropSz.height());

    painter.restore();
}
