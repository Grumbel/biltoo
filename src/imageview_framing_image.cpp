// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom enable, zoom-region. Image-mode framing: ImageController.

#include "imageview.h"
#include "imageitem.h"
#include "view/viewtransform.h"

#include <QScrollBar>

void ImageView::setStickyZoomEnabled(bool on)
{
    if (!m_image.framing().setStickyZoomEnabled(on)) {
        return;
    }
    emit stickyZoomChanged();
    emit statusChanged();
}

void ImageView::releaseStickyZoom()
{
    if (!m_image.framing().setStickyZoomEnabled(false)) {
        return;
    }
    emit stickyZoomChanged();
    emit statusChanged();
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
