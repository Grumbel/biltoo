// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Transform action thin routers. Geometry/content undo: item/geometryundocommand.cpp.

#include "imageview.h"
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

void ImageView::resetItemScale()
{
    m_workspace.resetItemScale();
}

void ImageView::resetItemRotation()
{
    m_workspace.resetItemRotation();
}

void ImageView::resetItemShear()
{
    m_workspace.resetItemShear();
}
