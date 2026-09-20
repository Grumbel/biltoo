// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Print page-guide overlay and resize handles.

#include "imageview.h"
#include "imageitem.h"
#include "pageguidegeometry.h"
#include "viewtransform.h"

#include <QPainter>
#include <QPrinter>
#include <QScrollBar>

void ImageView::setPageGuideVisible(bool on)
{
    if (!m_pageGuide.setVisible(on)) {
        return;
    }
    if (m_pageGuide.isVisible() && !m_pageGuide.hasValidSize()) {
        const qreal pxPerMm = pageGuidePxPerMm();
        m_pageGuide.setSize(QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm));
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}


void ImageView::setPageGuideFromPrinter(const QPrinter &printer)
{
    // fullRect = physical paper (size + orientation). paintRect is only the
    // printable inset and can look unchanged when the user picks a different
    // stock size with similar aspect (or when margins dominate).
    const QPageLayout layout = printer.pageLayout();
    QRectF mm = layout.fullRect(QPageLayout::Millimeter);
    if (!mm.isValid() || mm.width() <= 1.0 || mm.height() <= 1.0) {
        const QSizeF sz = layout.pageSize().size(QPageSize::Millimeter);
        if (sz.width() > 1.0 && sz.height() > 1.0) {
            mm = QRectF(QPointF(0, 0), sz);
            if (layout.orientation() == QPageLayout::Landscape && mm.width() < mm.height()) {
                mm = QRectF(0, 0, mm.height(), mm.width());
            }
        } else {
            mm = QRectF(0, 0, 210.0, 297.0);
        }
    }
    const qreal pxPerMm = pageGuidePxPerMm();
    m_pageGuide.setSize(QSizeF(mm.width() * pxPerMm, mm.height() * pxPerMm));
    m_pageGuide.setRect(QRectF()); // printer pages stay centred on the origin
    if (m_pageGuide.isVisible()) {
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        viewport()->update();
        emit statusChanged();
    }
}


void ImageView::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid()) {
        return;
    }

    if (isWorkspaceMode() && m_scene) {
        const QRectF source = (m_pageGuide.isVisible() && pageGuideSceneRect().isValid())
            ? pageGuideSceneRect()
            : contentExportBounds();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        // High-res materialize — same path as PNG export (not Soft scene samples).
        paintHighResExportItems(painter, source, pageRect);
        return;
    }

    if (isImageMode()) {
        ImageItem *item = primaryItem();
        if (!item) {
            item = targetItem();
        }
        if (item && !item->path().isEmpty()) {
            const QImage img = blockingExportDisplayForItem(item);
            if (!img.isNull()) {
                QSizeF fitted(img.size());
                fitted.scale(pageRect.size(), Qt::KeepAspectRatio);
                const QRectF target(
                    pageRect.center().x() - fitted.width() / 2.0,
                    pageRect.center().y() - fitted.height() / 2.0,
                    fitted.width(), fitted.height());
                painter->save();
                painter->translate(target.center());
                painter->rotate(item->itemRotation());
                painter->scale(item->itemHFlip() ? -1.0 : 1.0,
                               item->itemVFlip() ? -1.0 : 1.0);
                painter->translate(-target.center());
                painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter->drawImage(target, img);
                painter->restore();
                return;
            }
        }
    }

    if (m_scene) {
        QRectF source = m_scene->itemsBoundingRect();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        source.adjust(-4, -4, 4, 4);
        m_scene->render(painter, pageRect, source, Qt::KeepAspectRatio);
    }
}


QRectF ImageView::pageGuideSceneRect() const
{
    if (m_pageGuide.hasValidRect()) {
        return m_pageGuide.currentRect();
    }
    QSizeF sz = m_pageGuide.currentSize();
    if (!sz.isValid() || sz.width() <= 0 || sz.height() <= 0) {
        const qreal pxPerMm = pageGuidePxPerMm();
        sz = QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm);
    }
    return QRectF(-sz.width() / 2.0, -sz.height() / 2.0, sz.width(), sz.height());
}


void ImageView::fitPageGuideToContent(qreal marginPx)
{
    QRectF bounds = contentExportBounds();
    if (!bounds.isValid() || bounds.isEmpty()) {
        return;
    }
    if (marginPx > 0.0) {
        // contentExportBounds already pads 4px; add the requested extra margin.
        bounds.adjust(-marginPx, -marginPx, marginPx, marginPx);
    }
    m_pageGuide.setPage(bounds);
    m_pageGuide.setVisible(true);
    m_pageGuide.setSelected(true);
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}


void ImageView::setPageGuideSelected(bool on)
{
    if (!m_pageGuide.isVisible()) {
        on = false;
    }
    if (!m_pageGuide.setSelected(on)) {
        return;
    }
    viewport()->update();
}


int ImageView::pageGuideHandleAt(const QPoint &viewPos) const
{
    if (!m_pageGuide.isInteractive() || !isWorkspaceMode()) {
        return -1;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return -1;
    }
    const QRect viewRect = mapFromScene(page).boundingRect();
    return PageGuideGeometry::handleIndexAt(viewPos, viewRect);
}


bool ImageView::beginPageGuideResize(int handle)
{
    if (handle < 0 || handle > 7 || !m_pageGuide.isVisible()) {
        return false;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.width() < 1.0 || page.height() < 1.0) {
        return false;
    }
    m_pageGuide.setSelected(true);
    m_pageGuide.beginResize(handle, page);
    return true;
}


QRectF ImageView::pageGuideRectFromHandleDrag(const QPointF &scenePos,
                                              Qt::KeyboardModifiers mods) const
{
    // Match Workspace image scale handles:
    //   default = opposite edge/corner fixed
    //   Ctrl    = scale about centre
    //   Shift   = lock starting aspect (corners; with or without Ctrl)
    return PageGuideGeometry::rectFromHandleDrag(
        scenePos, m_pageGuide.dragStartRectRef(), m_pageGuide.currentDragHandle(),
        mods & Qt::ControlModifier, mods & Qt::ShiftModifier);
}


void ImageView::updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_pageGuide.isDragging()) {
        return;
    }
    const QRectF next = pageGuideRectFromHandleDrag(scenePos, mods);
    m_pageGuide.setPage(next);
    updateWorkspaceSceneRect();
    viewport()->update();
    emit statusChanged();
}


void ImageView::endPageGuideResize()
{
    m_pageGuide.endResize();
    viewport()->update();
}

