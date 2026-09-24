// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom enable, zoom-region. Image-mode framing: ImageController.

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "view/viewtransform.h"
#include "view/viewframing.h"

#include <QScrollBar>
#include "image/toolpolicy.h"
#include <QPointer>
#include <QTimer>
#include "content/contentxform.h"
#include "session/sessionappearance.h"

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
        ensureVisible(item, 32, 32);
    }
}

// --- View zoom / fit / fill (was imageview_framing.cpp) ---

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
    // View-level zoom in Image mode and free-form Workspace
    zoomViewBy(1.25);
}


void ImageView::zoomOut()
{
    zoomViewBy(1.0 / 1.25);
}


void ImageView::setWorkspaceDefaultViewScale()
{
    // Workspace is an overview canvas for multiple pages. Match four toolbar
    // zoom-out presses: each step is 1/1.25, so scale = (1/1.25)^4 ≈ 0.4096 (41%).
    constexpr qreal kStep = 1.25;
    const qreal s = 1.0 / (kStep * kStep * kStep * kStep);
    m_image.framing().clearFitFill();
    resetTransform();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(s, s);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}


void ImageView::zoomReset()
{
    m_image.zoomReset();
}

void ImageView::refreshScrollBarGeometry()
{
    // fitInView / sceneRect changes can leave AsNeeded bars with a stale range
    // until policy is toggled. Re-apply the current policies to force
    // QAbstractScrollArea to recompute visibility (public API only).
    const auto h = horizontalScrollBarPolicy();
    const auto v = verticalScrollBarPolicy();
    if (h == Qt::ScrollBarAsNeeded || v == Qt::ScrollBarAsNeeded) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(h);
        setVerticalScrollBarPolicy(v);
    }
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


// --- Image-mode framing: owned by ImageController (thin host forwards) ---

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
