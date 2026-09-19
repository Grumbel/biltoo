// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "grouptransformgeometry.h"
#include "imageitem.h"
#include "placementlinear.h"

#include <QTransform>
#include <QtMath>
#include <cmath>

int ImageView::groupHandleAt(const QPoint &viewPos, const QList<ImageItem *> &items) const
{
    const QRectF sceneBounds = selectionSceneBounds(items);
    if (!sceneBounds.isValid() || sceneBounds.isEmpty()) {
        return -1;
    }
    const QRect viewRect = mapFromScene(sceneBounds).boundingRect();
    return GroupTransformGeometry::handleIndexAt(viewPos, viewRect);
}

bool ImageView::beginGroupScale(int handle, const QList<ImageItem *> &items)
{
    if (handle < 0 || items.size() < 2) {
        return false;
    }
    const QRectF bounds = selectionSceneBounds(items);
    if (!bounds.isValid() || bounds.isEmpty()) {
        return false;
    }
    QList<WorkspaceItemState> states;
    states.reserve(items.size());
    for (ImageItem *item : items) {
        states.append(captureState(item));
    }
    m_groupXform.beginDrag(handle, GroupTransformGeometry::isRotateHandle(handle),
                           bounds, items, states);
    return true;
}

void ImageView::updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupXform.isScaleDrag() || !m_groupXform.dragListsAligned()) {
        return;
    }
    // Drop any pointers no longer on our canvas (deleted mid-drag).
    for (int i = m_groupXform.dragCount() - 1; i >= 0; --i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
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
            scenePos, m_groupXform.boundsStart, m_groupXform.handle,
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
        const WorkspaceItemState &st = m_groupXform.dragStartStateAt(i);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        const QPointF rel = st.pos - anchor;
        const QPointF newPos = anchor + QPointF(rel.x() * sx, rel.y() * sy);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        item->setPos(newPos);

        const qreal baseX = st.scale > 0 ? st.scale : 1.0;
        const qreal baseY = st.scaleY > 0 ? st.scaleY : baseX;
        if (!anisotropic) {
            // Uniform scene scale: scales only; rotation and shear stay from press.
            item->setItemScale(baseX * sx, baseY * sy);
            item->setItemShear(st.shear);
            item->setItemRotation(st.rotation);
            continue;
        }
        // Anisotropic: scale the *scene* images of local axes (e.x *= sx, e.y *= sy),
        // then re-decompose. Avoids QTransform multiply-order ambiguity and the
        // broken fallthrough that applied scene sx/sy as local scales (axis mix-up
        // on rotated tiles).
        QPointF e1, e2;
        PlacementLinear::unitAxes(baseX, baseY, st.shear, st.rotation, &e1, &e2);
        e1 = QPointF(e1.x() * sx, e1.y() * sy);
        e2 = QPointF(e2.x() * sx, e2.y() * sy);
        qreal nx = baseX;
        qreal ny = baseY;
        qreal nk = st.shear;
        qreal nrot = st.rotation;
        if (PlacementLinear::decomposeAxes(e1, e2, &nx, &ny, &nk, &nrot)) {
            PlacementLinear::clampScaleXY(&nx, &ny);
            item->setItemScale(nx, ny);
            item->setItemShear(nk);
            item->setItemRotation(nrot);
        }
        // If decompose fails, leave linear pose from last good frame (pos already updated).
    }
    m_framing.releaseFit();
    emit statusChanged();
}

void ImageView::endGroupScale()
{
    // Undo is committed from mouseReleaseEvent (TransformCommand is local there).
    m_groupXform.endDrag();
}

void ImageView::updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupXform.isRotateDrag() || !m_groupXform.dragListsAligned()) {
        return;
    }
    for (int i = m_groupXform.dragCount() - 1; i >= 0; --i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
            m_groupXform.nullDragItemAt(i);
        }
    }
    m_groupXform.pruneNullItems();
    if (!m_groupXform.dragListsAligned()) {
        return;
    }

    const QPointF centre = m_groupXform.centerStart;
    // Angle from group centre to pointer; seed from first press stored in
    // m_groupXform.pressScenePos when the drag starts (set in mouse path).
    qreal delta = 0.0;
    if (!GroupTransformGeometry::rotationDeltaFromDrag(
            centre, m_groupXform.pressScenePos, scenePos,
            mods & Qt::ShiftModifier, mods & Qt::ControlModifier, &delta)) {
        return;
    }

    for (int i = 0; i < m_groupXform.dragCount(); ++i) {
        ImageItem *item = m_groupXform.dragItemAt(i);
        const WorkspaceItemState &st = m_groupXform.dragStartStateAt(i);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        // Orbit position around group centre; add the same delta to placement angle.
        const QPointF newPos = GroupTransformGeometry::orbitPoint(centre, st.pos, delta);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        item->setPos(newPos);
        item->setItemRotation(st.rotation + delta);
    }
    m_framing.releaseFit();
    emit statusChanged();
}
