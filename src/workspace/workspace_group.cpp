// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "item/selectiongeometry.h"
#include "item/itemcomponents.h"
#include "workspace/grouptransformgeometry.h"
#include "workspace/pageguidegeometry.h"
#include "view/viewframing.h"
#include "imageitem.h"
#include "item/placementlinear.h"

#include <QTransform>
#include <QVector>
#include <QtMath>
#include <cmath>
#include <QPainter>
#include <QMouseEvent>
#include <QUndoStack>
#include <QWidget>


namespace {
QRectF selectionSceneBounds(const QList<ImageItem *> &items)
{
    QVector<QRectF> rects;
    rects.reserve(items.size());
    for (ImageItem *item : items) {
        if (item) {
            rects.append(item->contentSceneRect());
        }
    }
    return SelectionGeometry::unionContentAabbs(rects);
}
} // namespace


int WorkspaceController::groupHandleAt(const QPoint &viewPos, const QList<ImageItem *> &items) const
{
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return -1;
    }
    const QRect viewRect = m_view->mapFromScene(sceneBounds).boundingRect();
    return GroupTransformGeometry::handleIndexAt(viewPos, viewRect);
}

bool WorkspaceController::beginGroupScale(int handle, const QList<ImageItem *> &items)
{
    if (handle < 0 || items.size() < 2) {
        return false;
    }
    const QRectF bounds = selectionSceneBounds(items);
    if (!bounds.isValid() || bounds.isEmpty()) {
        return false;
    }
    QList<ItemComponents::Placement> placements;
    placements.reserve(items.size());
    for (ImageItem *item : items) {
        placements.append((item ? item->placement() : ItemComponents::Placement{}));
    }
    m_groupXform.beginDrag(handle, GroupTransformGeometry::isRotateHandle(handle),
                           bounds, items, placements);
    return true;
}

void WorkspaceController::updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupXform.isScaleDrag() || !m_groupXform.dragListsAligned()) {
        return;
    }
    // Drop any pointers no longer on our canvas (deleted mid-drag).
    for (int i = m_groupXform.dragCount() - 1; i >= 0; --i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        if (!item || !m_view->liveItems().contains(item) || item->scene() != m_view->canvasScene()) {
            m_groupXform.nullDragItemAt(i);
        }
    }
    m_groupXform.pruneNullItems();
    if (!m_groupXform.dragListsAligned()) {
        endGroupScale();
        return;
    }
    const GroupTransformGeometry::ScaleFactors sf =
        GroupTransformGeometry::scaleFactorsFromDrag(
            scenePos, m_groupXform.boundsStartRect(), m_groupXform.currentHandle(),
            mods & Qt::ShiftModifier);
    if (!sf.valid) {
        return;
    }
    const qreal sx = sf.sx;
    const qreal sy = sf.sy;
    const QPointF anchor = sf.anchor;

    const bool anisotropic = qAbs(sx - sy) > 1e-6;

    for (int i = 0; i < m_groupXform.dragCount(); ++i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        const ItemComponents::Placement &start = m_groupXform.dragStartPlacementAt(i);
        if (!item || !m_view->liveItems().contains(item)) {
            continue;
        }
        const QPointF rel = start.pos - anchor;
        const QPointF newPos = anchor + QPointF(rel.x() * sx, rel.y() * sy);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }

        ItemComponents::Placement pl = start;
        pl.pos = newPos;
        const qreal baseX = start.scale > 0 ? start.scale : 1.0;
        const qreal baseY = start.scaleY > 0 ? start.scaleY : baseX;
        if (!anisotropic) {
            // Uniform scene scale: scales only; rotation and shear stay from press.
            pl.scale = baseX * sx;
            pl.scaleY = baseY * sy;
            item->applyPlacement(pl);
            continue;
        }
        // Anisotropic: scale the *scene* images of local axes (e.x *= sx, e.y *= sy),
        // then re-decompose. Avoids QTransform multiply-order ambiguity and the
        // broken fallthrough that applied scene sx/sy as local scales (axis mix-up
        // on rotated tiles).
        QPointF e1, e2;
        PlacementLinear::unitAxes(baseX, baseY, start.shear, start.rotation, &e1, &e2);
        e1 = QPointF(e1.x() * sx, e1.y() * sy);
        e2 = QPointF(e2.x() * sx, e2.y() * sy);
        qreal nx = baseX;
        qreal ny = baseY;
        qreal nk = start.shear;
        qreal nrot = start.rotation;
        if (PlacementLinear::decomposeAxes(e1, e2, &nx, &ny, &nk, &nrot)) {
            PlacementLinear::clampScaleXY(&nx, &ny);
            pl.scale = nx;
            pl.scaleY = ny;
            pl.shear = nk;
            pl.rotation = nrot;
            item->applyPlacement(pl);
        } else {
            // Decompose failed: still move; leave linear pose from last good frame.
            item->applyPlacement(pl);
        }
    }
    m_view->hostFraming().releaseFit();
    m_view->notifyStatusChanged();
}

void WorkspaceController::endGroupScale()
{
    // Undo is committed from mouseReleaseEvent (TransformCommand is local there).
    m_groupXform.endDrag();
}

void WorkspaceController::updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupXform.isRotateDrag() || !m_groupXform.dragListsAligned()) {
        return;
    }
    for (int i = m_groupXform.dragCount() - 1; i >= 0; --i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        if (!item || !m_view->liveItems().contains(item) || item->scene() != m_view->canvasScene()) {
            m_groupXform.nullDragItemAt(i);
        }
    }
    m_groupXform.pruneNullItems();
    if (!m_groupXform.dragListsAligned()) {
        return;
    }

    const QPointF centre = m_groupXform.centerStartPoint();
    // Angle from group centre to pointer; seed from first press stored in
    // m_groupXform.pressScenePosPoint() when the drag starts (set in mouse path).
    qreal delta = 0.0;
    if (!GroupTransformGeometry::rotationDeltaFromDrag(
            centre, m_groupXform.pressScenePosPoint(), scenePos,
            mods & Qt::ShiftModifier, mods & Qt::ControlModifier, &delta)) {
        return;
    }

    for (int i = 0; i < m_groupXform.dragCount(); ++i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        const ItemComponents::Placement &start = m_groupXform.dragStartPlacementAt(i);
        if (!item || !m_view->liveItems().contains(item)) {
            continue;
        }
        // Orbit position around group centre; add the same delta to placement angle.
        const QPointF newPos = GroupTransformGeometry::orbitPoint(centre, start.pos, delta);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        ItemComponents::Placement pl = start;
        pl.pos = newPos;
        pl.rotation = start.rotation + delta;
        item->applyPlacement(pl);
    }
    m_view->hostFraming().releaseFit();
    m_view->notifyStatusChanged();
}

// --- selection chrome paint (from paint.cpp) ---

void WorkspaceController::paintGroupSelectionChrome(QPainter *painter, const QList<ImageItem *> &items) const
{
    if (!painter) {
        return;
    }
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return;
    }
    const QRect viewRect = m_view->mapFromScene(sceneBounds).boundingRect();
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // Group chrome: violet family so it is distinct from single-select blue
    // and crop amber.
    const QColor frameCol(150, 90, 220, 220);
    const QColor handleFill(180, 120, 255, 240);
    const QColor handleFillHot(210, 160, 255, 255);
    const QColor handleEdge(80, 40, 140);
    const QColor rotFill(200, 140, 255);
    const QColor rotFillHot(255, 230, 120);

    QPen framePen(frameCol, 0);
    framePen.setCosmetic(true);
    framePen.setWidthF(1.75);
    painter->setPen(framePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(viewRect);

    QPointF pts[8];
    PageGuideGeometry::handlePoints(viewRect, pts);
    // Corners 0,2,4,6: rounded line-arc-line; edges 1,3,5,7: short bars.
    auto unit = [](QPointF v) {
        const qreal len = qHypot(v.x(), v.y());
        return len > 1e-6 ? v / len : QPointF(1, 0);
    };
    auto drawCorner = [&](const QPointF &c, const QPointF &alongA, const QPointF &alongB, int id) {
        const bool hot = m_groupXform.isHandleHot(id);
        const QPointF d1 = unit(alongA);
        const QPointF d2 = unit(alongB);
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal arm = hs * 1.35;
        const qreal thick = hs * (hot ? 0.48 : 0.36);
        QPainterPath path;
        path.moveTo(c + d1 * arm);
        path.lineTo(c);
        path.lineTo(c + d2 * arm);
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(thick);
        hp.setCapStyle(Qt::RoundCap);
        hp.setJoinStyle(Qt::RoundJoin);
        painter->setPen(hp);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(path);
        if (hot) {
            QPen glow(handleFill, 0);
            glow.setCosmetic(true);
            glow.setWidthF(thick * 0.55);
            glow.setCapStyle(Qt::RoundCap);
            glow.setJoinStyle(Qt::RoundJoin);
            painter->setPen(glow);
            painter->drawPath(path);
        }
    };
    auto drawEdgeBar = [&](const QPointF &mid, const QPointF &along, int id) {
        const bool hot = m_groupXform.isHandleHot(id);
        const QPointF a = unit(along);
        const QPointF perp(-a.y(), a.x());
        const qreal hs = hot ? 12.0 : 10.0;
        const qreal len = hs * 1.8;
        const qreal thick = hs * 0.35;
        QPen hp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        hp.setCosmetic(true);
        hp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(hp);
        painter->setBrush(hot ? handleFillHot : handleFill);
        QPolygonF bar;
        bar << mid + a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) + perp * (thick / 2)
            << mid - a * (len / 2) - perp * (thick / 2)
            << mid + a * (len / 2) - perp * (thick / 2);
        painter->drawPolygon(bar);
        painter->setBrush(Qt::NoBrush);
    };
    // pts: 0 TL, 1 T, 2 TR, 3 R, 4 BR, 5 B, 6 BL, 7 L
    drawCorner(pts[0], QPointF(1, 0), QPointF(0, 1), 0);
    drawEdgeBar(pts[1], QPointF(1, 0), 1);
    drawCorner(pts[2], QPointF(-1, 0), QPointF(0, 1), 2);
    drawEdgeBar(pts[3], QPointF(0, 1), 3);
    drawCorner(pts[4], QPointF(-1, 0), QPointF(0, -1), 4);
    drawEdgeBar(pts[5], QPointF(1, 0), 5);
    drawCorner(pts[6], QPointF(1, 0), QPointF(0, -1), 6);
    drawEdgeBar(pts[7], QPointF(0, 1), 7);

    // Rotate knobs outside mid-edges (same offset language as single-item).
    constexpr qreal kRotateOffset = 28.0;
    const QPointF rot[4] = {
        QPointF(viewRect.center().x(), viewRect.top() - kRotateOffset),
        QPointF(viewRect.right() + kRotateOffset, viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom() + kRotateOffset),
        QPointF(viewRect.left() - kRotateOffset, viewRect.center().y()),
    };
    const QPointF edgeMid[4] = {
        QPointF(viewRect.center().x(), viewRect.top()),
        QPointF(viewRect.right(), viewRect.center().y()),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    QPen stem(frameCol, 0);
    stem.setCosmetic(true);
    stem.setWidthF(1.25);
    for (int i = 0; i < 4; ++i) {
        const int handleId = 8 + i;
        const bool hot = m_groupXform.isHandleHot(handleId);
        painter->setPen(stem);
        painter->drawLine(edgeMid[i], rot[i]);
        const qreal rad = hot ? 7.0 : 5.0;
        painter->setBrush(hot ? rotFillHot : rotFill);
        QPen rp(hot ? QColor(255, 255, 255) : handleEdge, 0);
        rp.setCosmetic(true);
        rp.setWidthF(hot ? 1.6 : 1.15);
        painter->setPen(rp);
        painter->drawEllipse(rot[i], rad, rad);
    }

    painter->restore();
}

bool WorkspaceController::tryMouseMoveGroupAndHandleDrag(QMouseEvent *event)
{
    if (m_groupXform.isScaleDrag()) {
        updateGroupScale(m_view->mapToScene(event->pos()), event->modifiers());
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
        event->accept();
        return true;
    }
    if (m_groupXform.isRotateDrag()) {
        updateGroupRotate(m_view->mapToScene(event->pos()), event->modifiers());
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
        event->accept();
        return true;
    }
    if (m_itemInteract.isHandleDragging()) {
        m_itemInteract.currentHandleDragItem()->updateHandleInteraction(
            m_view->mapToScene(event->pos()), event->modifiers(), m_itemInteract.handlePressRef());
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
        event->accept();
        return true;
    }
    return false;
}

bool WorkspaceController::tryMouseReleaseGroupDrag(QMouseEvent *event)
{
    if (!(m_groupXform.isScaleDrag() || m_groupXform.isRotateDrag())
        || event->button() != Qt::LeftButton) {
        return false;
    }
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        if (m_groupXform.hasDragItems()) {
            stack->beginMacro(m_groupXform.isRotateDrag()
                                  ? m_view->tr("Rotate selection")
                                  : m_view->tr("Scale selection"));
            for (int i = 0; i < m_groupXform.dragCount(); ++i) {
                ImageItem *item = m_groupXform.dragItemAt(i);
                if (!item) {
                    continue;
                }
                m_view->hostPushItemTransformUndo(
                    item, m_groupXform.dragStartPlacementAt(i), item->placement(),
                    m_view->tr("Transform"));
            }
            stack->endMacro();
        }
    }
    endGroupScale();
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}

bool WorkspaceController::tryMouseReleaseHandleDrag(QMouseEvent *event)
{
    if (!m_itemInteract.isHandleDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    ImageItem *handleItem = m_itemInteract.currentHandleDragItem();
    handleItem->endHandleInteraction(m_itemInteract.handlePressRef().handle);
    m_view->hostPushItemTransformUndo(handleItem, m_itemInteract.currentDragStartPlacement(),
                                      handleItem->placement(), m_view->tr("Transform"));
    m_itemInteract.endHandleDrag();
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}

bool WorkspaceController::tryMouseReleaseItemDrag(QMouseEvent *event)
{
    // Completes the move undo + endMove. Returns false so ImageView still
    // delivers the release to QGraphicsView (selection / scene).
    if (!m_itemInteract.currentDragItem() || event->button() != Qt::LeftButton) {
        return false;
    }
    m_view->hostPushItemTransformUndo(m_itemInteract.currentDragItem(),
                                      m_itemInteract.currentDragStartPlacement(),
                                      m_itemInteract.currentDragItem()->placement(),
                                      m_view->tr("Move"));
    m_itemInteract.endMove();
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    return false;
}

