// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View zoom, fit/fill, sticky zoom/pan, and image-mode framing.

#include "imageview.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "viewtransform.h"
#include "viewframing.h"

#include <QScrollBar>
#include "toolpolicy.h"
#include <QPointer>
#include <QTimer>
#include "contentxform.h"
#include "sessionappearance.h"

qreal ImageView::viewScale() const
{
    return ViewTransform::scaleFrom(transform());
}


void ImageView::zoomViewBy(qreal factor)
{
    // Gallery: view zoom is allowed for inspection (matches wheel zoom). Pack /
    // resize still resets the view transform so tiles stay layout-correct
    // (AUDIT M4 — one policy: zoom works until next pack).
    releaseStickyZoom();
    m_framing.clearFitFill();
    // Keep the viewport centre stable when zooming via toolbar/shortcuts
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(factor, factor);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Viewport-space chrome only — no selected-item prepareGeometryChange.
    if (viewport()) {
        viewport()->update();
    }
    // Zoom changes on-screen cell size → ladder / tile LOD after settle.
    // Gallery already debounced interest; Image/Workspace match that pattern
    // so continuous zoom does not issue tile work every notch.
    if (isGalleryMode()) {
        scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowScrollMs);
    } else {
        m_displayPipeline.scheduleTileLodAfterInteraction(50);
    }
    emit statusChanged();
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
    m_framing.clearFitFill();
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
    m_framing.clearFitFill();
    if (isMultiItemMode()) {
        // Gallery/Workspace: one-shot identity view (sticky zoom is Image-only).
        resetTransform();
        if (isGalleryMode()) {
            updateGalleryDecodeWindow();
        }
        emit statusChanged();
        return;
    }
    // Image mode 1:1 — item at native scale, view identity, then centre
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        resetTransform();
        centerOn(item);
        emit statusChanged();
    }
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
    m_framing.setFitOnly();
    if (isGalleryMode()) {
        // Fit the packed gallery into the viewport (whole pack). Sticky zoom
        // is Image-mode only — Gallery uses one-shot framing + ensureVisible.
        if (!m_items.isEmpty()) {
            const QRectF bounds = ViewTransform::padded(m_scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatio);
            }
            updateGalleryDecodeWindow();
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(ViewTransform::padded(m_scene->itemsBoundingRect(), 32),
                      Qt::KeepAspectRatio);
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatio);
        refreshScrollBarGeometry();
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatio);
        refreshScrollBarGeometry();
        emit statusChanged();
    }
}


void ImageView::zoomFill()
{
    m_framing.setFillMode();
    if (isGalleryMode()) {
        if (!m_items.isEmpty()) {
            const QRectF bounds = ViewTransform::padded(m_scene->itemsBoundingRect(), GalleryLayout::Params::kDefaultMargin);
            if (bounds.isValid() && !bounds.isEmpty()) {
                m_scene->setSceneRect(bounds);
                fitInView(bounds, Qt::KeepAspectRatioByExpanding);
            }
            updateGalleryDecodeWindow();
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (isWorkspaceMode()) {
        if (!m_items.isEmpty()) {
            fitInView(ViewTransform::padded(m_scene->itemsBoundingRect(), 32),
                      Qt::KeepAspectRatioByExpanding);
            refreshScrollBarGeometry();
            emit statusChanged();
        }
        return;
    }
    if (ImageItem *item = targetItem()) {
        {
            ItemComponents::Placement pl = item->placement();
            pl.scale = 1.0;
            pl.scaleY = 1.0;
            item->applyPlacement(pl);
        }
        fitItem(item, Qt::KeepAspectRatioByExpanding);
        refreshScrollBarGeometry();
        emit statusChanged();
    } else if (m_items.size() > 1) {
        fitInView(m_scene->itemsBoundingRect(), Qt::KeepAspectRatioByExpanding);
        refreshScrollBarGeometry();
        emit statusChanged();
    }
}


void ImageView::armZoomRegion()
{
    if (m_items.isEmpty() && isImageMode() && !hasClassicPath()) {
        return;
    }
    cancelZoomRegion();
    m_zoomRegion.arm();
    setCursor(Qt::CrossCursor);
    emit statusChanged();
    viewport()->update();
}
