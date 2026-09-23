// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Print page-guide overlay and resize handles.

#include "imageview.h"
#include "imageitem.h"
#include "workspace/pageguidegeometry.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QPrinter>
#include <QScrollBar>
#include <QMouseEvent>

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
                const ItemComponents::Placement pl = item->placement();
                painter->rotate(pl.rotation);
                painter->scale(pl.hFlip ? -1.0 : 1.0, pl.vFlip ? -1.0 : 1.0);
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


// --- paint handles (from paint.cpp) ---

void ImageView::paintPageGuideHandles(QPainter *painter) const
{
    if (!painter || !m_pageGuide.isInteractive()) {
        return;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return;
    }
    const QRect viewRect = mapFromScene(page).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Same language as group scale grips, in the page-guide blue family.
    const QColor handleEdge(20, 60, 120);
    const QColor handleHot(255, 255, 255);

    const QPointF pts[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = m_pageGuide.isHandleHot(id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
    };
    auto drawEdge = [&](const QPointF &c, const QPointF &along, int id) {
        const bool hot = m_pageGuide.isHandleHot(id);
        const QPointF d = unit(along);
        const qreal hs = hot ? 11.0 : 9.0;
        const qreal half = hs * 1.1;
        QPen hp(hot ? handleHot : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hs * (hot ? 0.42 : 0.32));
        hp.setCapStyle(Qt::RoundCap);
        painter->setPen(hp);
        painter->drawLine(c - d * half, c + d * half);
    };
    // Corners: 0 TL, 2 TR, 4 BR, 6 BL
    drawCorner(pts[0], pts[1] - pts[0], pts[7] - pts[0], 0);
    drawCorner(pts[2], pts[1] - pts[2], pts[3] - pts[2], 2);
    drawCorner(pts[4], pts[5] - pts[4], pts[3] - pts[4], 4);
    drawCorner(pts[6], pts[5] - pts[6], pts[7] - pts[6], 6);
    // Edges: 1 T, 3 R, 5 B, 7 L
    drawEdge(pts[1], pts[2] - pts[0], 1);
    drawEdge(pts[3], pts[4] - pts[2], 3);
    drawEdge(pts[5], pts[4] - pts[6], 5);
    drawEdge(pts[7], pts[6] - pts[0], 7);
    painter->restore();
}

// --- input try* (pageguide) from imageview_input.cpp ---

bool ImageView::tryMouseMovePageGuide(QMouseEvent *event)
{
    if (m_pageGuide.isDragging()) {
        updatePageGuideResize(mapToScene(event->pos()), event->modifiers());
        event->accept();
        return true;
    }
    if (isWorkspaceMode() && m_pageGuide.isInteractive()
        && !(event->buttons() & Qt::LeftButton)) {
        const int ph = pageGuideHandleAt(event->pos());
        if (m_pageGuide.setHoverHandle(ph)) {
            viewport()->update();
        }
        if (ph >= 0) {
            switch (ph) {
            case 0: case 4:
                viewport()->setCursor(Qt::SizeFDiagCursor);
                break;
            case 2: case 6:
                viewport()->setCursor(Qt::SizeBDiagCursor);
                break;
            case 1: case 5:
                viewport()->setCursor(Qt::SizeVerCursor);
                break;
            case 3: case 7:
                viewport()->setCursor(Qt::SizeHorCursor);
                break;
            default:
                break;
            }
            event->accept();
            return true;
        }
    }
    return false;
}

bool ImageView::tryMouseReleasePageGuide(QMouseEvent *event)
{
    if (!m_pageGuide.isDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    endPageGuideResize();
    event->accept();
    return true;
}
