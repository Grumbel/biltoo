// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Print page-guide overlay and resize handles (WorkspaceController-owned).

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include <QGraphicsScene>
#include "imageitem.h"
#include "workspace/pageguidegeometry.h"
#include "view/viewtransform.h"

#include <QPainter>
#include <QPainterPath>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>
#include <QMouseEvent>
#include <QWidget>
#include <QtMath>

void WorkspaceController::setPageGuideVisible(bool on)
{
    if (!m_pageGuide.setVisible(on)) {
        return;
    }
    if (m_pageGuide.isVisible() && !m_pageGuide.hasValidSize()) {
        const qreal pxPerMm = pageGuidePxPerMm();
        m_pageGuide.setSize(QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm));
    }
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    if (m_view->viewport()) { m_view->viewport()->update(); };
    m_view->notifyStatusChanged();
}


void WorkspaceController::setPageGuideFromPrinter(const QPrinter &printer)
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
        if (m_view->isWorkspaceMode()) {
            m_view->updateWorkspaceSceneRect();
        }
        if (m_view->viewport()) { m_view->viewport()->update(); };
        m_view->notifyStatusChanged();
    }
}


QRectF WorkspaceController::pageGuideSceneRect() const
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


void WorkspaceController::fitPageGuideToContent(qreal marginPx)
{
    QRectF bounds;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const QRectF r = item->contentSceneRect();
        if (!r.isValid() || r.isEmpty()) {
            continue;
        }
        bounds = bounds.isValid() ? bounds.united(r) : r;
    }
    if (!bounds.isValid() || bounds.isEmpty()) {
        if (m_view->canvasScene()) {
            bounds = m_view->canvasScene()->itemsBoundingRect();
        }
    }
    if (bounds.isValid() && !bounds.isEmpty()) {
        bounds.adjust(-4, -4, 4, 4);
    }
    if (!bounds.isValid() || bounds.isEmpty()) {
        return;
    }
    if (marginPx > 0.0) {
        bounds.adjust(-marginPx, -marginPx, marginPx, marginPx);
    }
    m_pageGuide.setPage(bounds);
    m_pageGuide.setVisible(true);
    m_pageGuide.setSelected(true);
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    if (m_view->viewport()) { m_view->viewport()->update(); };
    m_view->notifyStatusChanged();
}


void WorkspaceController::setPageGuideSelected(bool on)
{
    if (!m_pageGuide.isVisible()) {
        on = false;
    }
    if (!m_pageGuide.setSelected(on)) {
        return;
    }
    if (m_view->viewport()) { m_view->viewport()->update(); };
}


int WorkspaceController::pageGuideHandleAt(const QPoint &viewPos) const
{
    if (!m_pageGuide.isInteractive() || !m_view->isWorkspaceMode()) {
        return -1;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return -1;
    }
    const QRect viewRect = m_view->mapFromScene(page).boundingRect();
    return PageGuideGeometry::handleIndexAt(viewPos, viewRect);
}


bool WorkspaceController::beginPageGuideResize(int handle)
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


QRectF WorkspaceController::pageGuideRectFromHandleDrag(const QPointF &scenePos,
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


void WorkspaceController::updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_pageGuide.isDragging()) {
        return;
    }
    const QRectF next = pageGuideRectFromHandleDrag(scenePos, mods);
    m_pageGuide.setPage(next);
    m_view->updateWorkspaceSceneRect();
    if (m_view->viewport()) { m_view->viewport()->update(); };
    m_view->notifyStatusChanged();
}


void WorkspaceController::endPageGuideResize()
{
    m_pageGuide.endResize();
    if (m_view->viewport()) { m_view->viewport()->update(); };
}


// --- paint handles (from paint.cpp) ---

void WorkspaceController::paintPageGuideHandles(QPainter *painter) const
{
    if (!painter || !m_pageGuide.isInteractive()) {
        return;
    }
    const QRectF page = pageGuideSceneRect();
    if (!page.isValid() || page.isEmpty()) {
        return;
    }
    const QRect viewRect = m_view->mapFromScene(page).boundingRect();
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

qreal WorkspaceController::pageGuidePxPerMm()
{
    // Workspace items use native image pixels as scene units. A 12MP photo is
    // ~4000px wide; at screen 96dpi an A4 sheet is only ~794px and looks tiny.
    // Use 300dpi so a page is roughly photo-scale (~2480×3508 for A4) while
    // still mapping 1:1 to physical paper on print/PDF.
    return PageGuideGeometry::pixelsPerMm();
}

bool WorkspaceController::tryMouseMovePageGuide(QMouseEvent *event)
{
    if (m_pageGuide.isDragging()) {
        updatePageGuideResize(m_view->mapToScene(event->pos()), event->modifiers());
        event->accept();
        return true;
    }
    if (m_view->isWorkspaceMode() && m_pageGuide.isInteractive()
        && !(event->buttons() & Qt::LeftButton)) {
        const int ph = pageGuideHandleAt(event->pos());
        if (m_pageGuide.setHoverHandle(ph)) {
            if (QWidget *vp = m_view->viewport()) {
                vp->update();
            }
        }
        if (ph >= 0) {
            if (QWidget *vp = m_view->viewport()) {
                switch (ph) {
                case 0: case 4:
                    vp->setCursor(Qt::SizeFDiagCursor);
                    break;
                case 2: case 6:
                    vp->setCursor(Qt::SizeBDiagCursor);
                    break;
                case 1: case 5:
                    vp->setCursor(Qt::SizeVerCursor);
                    break;
                case 3: case 7:
                    vp->setCursor(Qt::SizeHorCursor);
                    break;
                default:
                    break;
                }
            }
            event->accept();
            return true;
        }
    }
    return false;
}

bool WorkspaceController::tryMouseReleasePageGuide(QMouseEvent *event)
{
    if (!m_pageGuide.isDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    endPageGuideResize();
    event->accept();
    return true;
}
