// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Transform action thin routers. Geometry/content undo: item/geometryundocommand.cpp.

#include "imageview.h"
#include "item/placementlinear.h"
#include "imageitem.h"

void ImageView::flipHorizontal()
{
    m_image.flipHorizontal();
}

void ImageView::flipVertical()
{
    m_image.flipVertical();
}

void ImageView::rotateContentByQuarterTurns(ImageItem *item, int quarterTurns)
{
    m_image.rotateContentByQuarterTurns(item, quarterTurns);
}

void ImageView::rotateLeft()
{
    m_image.rotateLeft();
}

void ImageView::rotateRight()
{
    m_image.rotateRight();
}

void ImageView::raiseItem(ImageItem *item)
{
    m_workspace.raiseItem(item);
}

void ImageView::lowerItem(ImageItem *item)
{
    m_workspace.lowerItem(item);
}

void ImageView::raiseSelected()
{
    m_workspace.raiseSelected();
}

void ImageView::lowerSelected()
{
    m_workspace.lowerSelected();
}

void ImageView::opacityUp()
{
    m_workspace.opacityUp();
}

void ImageView::opacityDown()
{
    m_workspace.opacityDown();
}

void ImageView::opacityReset()
{
    m_workspace.opacityReset();
}

void ImageView::resetItemScale(ImageItem *item)
{
    m_workspace.resetItemScale(item);
}

void ImageView::resetItemRotation(ImageItem *item)
{
    m_workspace.resetItemRotation(item);
}

void ImageView::resetItemShear(ImageItem *item)
{
    m_workspace.resetItemShear(item);
}

qreal ImageView::snapRotationDegrees(qreal degrees)
{
    // Image mode: always nearest 90° content orientation. Free Workspace tilt
    // (residual off the cardinal) is discarded — never shown as an arbitrary angle.
    return PlacementLinear::cardinalRotationOrZero(degrees);
}
