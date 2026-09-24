// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with image/ ownership.

#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"
#include <QScrollBar>

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


// --- from imageview_framing_image.cpp ---
void ImageView::setStickyZoomEnabled(bool on)
{
    m_image.setStickyZoomEnabled(on);
}

void ImageView::releaseStickyZoom()
{
    m_image.releaseStickyZoom();
}

void ImageView::cancelZoomRegion()
{
    m_image.cancelZoomRegion();
}

void ImageView::fitItem(ImageItem *item, Qt::AspectRatioMode mode)
{
    m_image.fitItem(item, mode);
}

void ImageView::ensureVisibleItem(ImageItem *item)
{
    if (item) {
        ensureVisible(static_cast<const QGraphicsItem *>(item),
                      ViewTransform::kEnsureVisibleMargin,
                      ViewTransform::kEnsureVisibleMargin);
    }
}

qreal ImageView::viewScale() const
{
    return ViewTransform::scaleFrom(transform());
}

void ImageView::zoomViewBy(qreal factor)
{
    m_image.zoomViewBy(factor);
}

void ImageView::zoomIn()
{
    m_image.zoomIn();
}

void ImageView::zoomOut()
{
    m_image.zoomOut();
}

void ImageView::setWorkspaceDefaultViewScale()
{
    m_image.setWorkspaceDefaultViewScale();
}

void ImageView::zoomReset()
{
    m_image.zoomReset();
}

void ImageView::refreshScrollBarGeometry()
{
    m_shell.refreshScrollBarGeometry();
}

void ImageView::zoomFit()
{
    m_image.zoomFit();
}

void ImageView::zoomFill()
{
    m_image.zoomFill();
}

void ImageView::armZoomRegion()
{
    m_image.armZoomRegion();
}

void ImageView::captureStickyPanAnchor(ImageItem *item)
{
    m_image.captureStickyPanAnchor(item);
}

void ImageView::restoreStickyPanAnchor(ImageItem *item)
{
    m_image.restoreStickyPanAnchor(item);
}

void ImageView::applyImageModeFraming(ImageItem *item)
{
    m_image.applyImageModeFraming(item);
}

void ImageView::preserveImageViewOnLogicalSizeChange(ImageItem *item,
                                                     const QSize &before,
                                                     const QSize &after)
{
    m_image.preserveImageViewOnLogicalSizeChange(item, before, after);
}

void ImageView::syncImageModeSceneRect(ImageItem *item)
{
    m_image.syncImageModeSceneRect(item);
}
