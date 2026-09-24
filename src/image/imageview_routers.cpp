// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with image/ ownership.

#include "imageview.h"
#include "imageitem.h"

// --- from src/imageview_transform_actions.cpp ---
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

// --- from src/imageview_appearance_commit.cpp ---
void ImageView::commitItemSessionEdit(ImageItem *item)
{
    m_image.commitItemSessionEdit(item);
}



void ImageView::propagateSessionAppearanceToViews(ImageItem *item)
{
    m_image.propagateSessionAppearanceToViews(item);
}



void ImageView::copySessionAppearance(SessionImageId fromId, SessionImageId toId)
{
    m_image.copySessionAppearance(fromId, toId);
}







// --- content appearance targets (from transform) ---

bool ImageView::targetHasContentAppearance() const
{
    return m_image.targetHasContentAppearance();
}



int ImageView::resetContentAppearanceForTargets()
{
    return m_image.resetContentAppearanceForTargets();
}



// --- from src/imageview_color_grade.cpp ---
void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_image.scheduleColorAdjustCommit(sid, path);
}

void ImageView::flushColorAdjustCommit()
{
    m_image.flushColorAdjustCommit();
}

void ImageView::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    m_image.setTargetColorAdjustments(adj);
}

