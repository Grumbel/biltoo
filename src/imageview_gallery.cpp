// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include <QPainter>
#include "imageitem.h"
#include <QGraphicsItem>

void ImageView::discardStashedGallery()
{
    m_gallery.discardStash();
}

void ImageView::stashGalleryItems()
{
    m_gallery.stashItems();
}

void ImageView::restoreStashedGalleryItems()
{
    m_gallery.restoreStashedItems();
}

void ImageView::snapshotGalleryViewport()
{
    m_gallery.snapshotViewport();
}

void ImageView::restoreGalleryViewport(const QString &focusPath)
{
    m_gallery.restoreViewport(focusPath);
}

void ImageView::applyPendingGalleryRestore()
{
    m_gallery.applyPendingRestore();
}

void ImageView::reassertGalleryViewport()
{
    m_gallery.reassertViewport();
}

void ImageView::leaveForImageMode()
{
    m_gallery.leaveForImageMode();
}

void ImageView::returnToGalleryFromImage(LayoutMode layout, const QString &focusPath)
{
    m_gallery.returnFromImage(static_cast<int>(layout), focusPath);
}

void ImageView::returnToWorkspaceFromImage()
{
    setViewMode(ViewMode::Workspace);
}

void ImageView::enterGallery(LayoutMode packagedLayout)
{
    // Sticky Fit/Fill/1:1 is Image-mode only.
    releaseStickyZoom();
    m_gallery.enter(static_cast<int>(packagedLayout));
}

// --- gallery selection frames (from paint) ---

void ImageView::paintGallerySelectionFrames(QPainter *painter, const QRectF &exposed) const
{
    if (!painter || !m_scene) {
        return;
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    painter->save();
    QPen pen(QColor(0, 180, 255, 255), 0);
    pen.setCosmetic(true);
    pen.setWidthF(4.0);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (QGraphicsItem *gi : selected) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->isInteractive()) {
            continue;
        }
        // Same rect the content paint uses (gallery clip when Grid-Crop).
        const QRectF local = item->displayContentRect();
        const QPolygonF scenePoly = item->mapToScene(local);
        const QRectF bounds = scenePoly.boundingRect();
        if (!exposed.isNull() && !exposed.intersects(bounds)) {
            continue;
        }
        // Inset ~2 local units equivalent is awkward after map; cosmetic stroke
        // sits on the edge. drawPolygon follows rotated/sheared cells.
        painter->drawPolygon(scenePoly);
    }
    painter->restore();
}

