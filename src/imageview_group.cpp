// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
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
    constexpr qreal kScaleHit = 10.0;
    constexpr qreal kRotateOffset = 28.0;
    constexpr qreal kRotateHit = 12.0;
    const QPointF corners[8] = {
        viewRect.topLeft(),
        QPointF(viewRect.center().x(), viewRect.top()),
        viewRect.topRight(),
        QPointF(viewRect.right(), viewRect.center().y()),
        viewRect.bottomRight(),
        QPointF(viewRect.center().x(), viewRect.bottom()),
        viewRect.bottomLeft(),
        QPointF(viewRect.left(), viewRect.center().y()),
    };
    // Prefer rotate knobs (outside) so they are not stolen by edge scale hits.
    const QPointF rot[4] = {
        QPointF(viewRect.center().x(), viewRect.top() - kRotateOffset),    // 8 T
        QPointF(viewRect.right() + kRotateOffset, viewRect.center().y()),  // 9 R
        QPointF(viewRect.center().x(), viewRect.bottom() + kRotateOffset), // 10 B
        QPointF(viewRect.left() - kRotateOffset, viewRect.center().y()),   // 11 L
    };
    for (int i = 0; i < 4; ++i) {
        if (QLineF(QPointF(viewPos), rot[i]).length() <= kRotateHit) {
            return 8 + i;
        }
    }
    for (int i = 0; i < 8; ++i) {
        if (QLineF(QPointF(viewPos), corners[i]).length() <= kScaleHit) {
            return i;
        }
    }
    return -1;
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
    m_groupHandle = handle;
    m_groupScaleDrag = !isGroupRotateHandle(handle);
    m_groupRotateDrag = isGroupRotateHandle(handle);
    m_groupBoundsStart = bounds;
    m_groupCenterStart = bounds.center();
    m_groupDragItems = items;
    m_groupDragStartStates.clear();
    for (ImageItem *item : items) {
        m_groupDragStartStates.append(captureState(item));
    }
    return true;
}

void ImageView::updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupScaleDrag || m_groupDragItems.isEmpty()
        || m_groupDragStartStates.size() != m_groupDragItems.size()) {
        return;
    }
    // Drop any pointers no longer on our canvas (deleted mid-drag).
    for (int i = m_groupDragItems.size() - 1; i >= 0; --i) {
        ImageItem *item = m_groupDragItems.at(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
            m_groupDragItems.removeAt(i);
            m_groupDragStartStates.removeAt(i);
        }
    }
    if (m_groupDragItems.isEmpty()) {
        endGroupScale();
        return;
    }
    const QRectF b = m_groupBoundsStart;
    // Fixed opposite corner / edge as anchor (selection AABB at press).
    QPointF anchor = m_groupCenterStart;
    switch (m_groupHandle) {
    case 0: anchor = b.bottomRight(); break; // TL
    case 1: anchor = QPointF(b.center().x(), b.bottom()); break; // T
    case 2: anchor = b.bottomLeft(); break; // TR
    case 3: anchor = QPointF(b.left(), b.center().y()); break; // R
    case 4: anchor = b.topLeft(); break; // BR
    case 5: anchor = QPointF(b.center().x(), b.top()); break; // B
    case 6: anchor = b.topRight(); break; // BL
    case 7: anchor = QPointF(b.right(), b.center().y()); break; // L
    default: break;
    }

    qreal sx = 1.0;
    qreal sy = 1.0;
    const qreal eps = 1.0;
    const bool edgeHandle = (m_groupHandle == 1 || m_groupHandle == 3
                             || m_groupHandle == 5 || m_groupHandle == 7);
    const bool cornerHandle = (m_groupHandle == 0 || m_groupHandle == 2
                               || m_groupHandle == 4 || m_groupHandle == 6);
    switch (m_groupHandle) {
    case 0: // TL
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 1: // T — vertical stretch of the selection AABB
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        sx = 1.0;
        break;
    case 2: // TR
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 3: // R — horizontal stretch
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = 1.0;
        break;
    case 4: // BR
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 5: // B — vertical stretch
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        sx = 1.0;
        break;
    case 6: // BL
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 7: // L — horizontal stretch
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = 1.0;
        break;
    default:
        break;
    }

    // Modifier semantics (match drawing-app usual, inverse of prior uniform default):
    //   Edge handles:   default = axis stretch (H or V); Shift = lock aspect (uniform)
    //   Corner handles: default = uniform;               Shift = free H/V axes
    const bool shift = mods & Qt::ShiftModifier;
    if (edgeHandle && shift) {
        const qreal s = (m_groupHandle == 1 || m_groupHandle == 5) ? sy : sx;
        sx = s;
        sy = s;
    } else if (cornerHandle && !shift) {
        const qreal s = (qAbs(sx) + qAbs(sy)) * 0.5;
        if (s > 1e-9) {
            sx = s;
            sy = s;
        }
    }
    sx = qBound(0.05, qAbs(sx), 20.0);
    sy = qBound(0.05, qAbs(sy), 20.0);

    if (!qIsFinite(sx) || !qIsFinite(sy) || !qIsFinite(anchor.x()) || !qIsFinite(anchor.y())) {
        return;
    }

    const bool anisotropic = qAbs(sx - sy) > 1e-6;
    QTransform sceneScale;
    sceneScale.scale(sx, sy);

    for (int i = 0; i < m_groupDragItems.size(); ++i) {
        ImageItem *item = m_groupDragItems.at(i);
        const WorkspaceItemState &st = m_groupDragStartStates.at(i);
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
        // Anisotropic (edge stretch or Shift+corner): L' = S_scene · L, then
        // re-decompose so rotated/sheared tiles track the AABB stretch.
        const QTransform L = PlacementLinear::make(baseX, baseY, st.shear, st.rotation);
        const QTransform Lp = sceneScale * L;
        qreal nx = baseX;
        qreal ny = baseY;
        qreal nk = st.shear;
        qreal nrot = st.rotation;
        if (PlacementLinear::decompose(Lp, &nx, &ny, &nk, &nrot)) {
            item->setItemScale(qBound(0.01, nx, 50.0), qBound(0.01, ny, 50.0));
            item->setItemShear(nk);
            item->setItemRotation(nrot);
        } else {
            item->setItemScale(baseX * sx, baseY * sy);
            item->setItemShear(st.shear);
            item->setItemRotation(st.rotation);
        }
    }
    m_fitMode = false;
    emit statusChanged();
}

void ImageView::endGroupScale()
{
    // Undo is committed from mouseReleaseEvent (TransformCommand is local there).
    m_groupScaleDrag = false;
    m_groupRotateDrag = false;
    m_groupHandle = -1;
    m_groupHoverHandle = -1;
    m_groupDragItems.clear();
    m_groupDragStartStates.clear();
}

void ImageView::updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods)
{
    if (!m_groupRotateDrag || m_groupDragItems.isEmpty()
        || m_groupDragStartStates.size() != m_groupDragItems.size()) {
        return;
    }
    for (int i = m_groupDragItems.size() - 1; i >= 0; --i) {
        ImageItem *item = m_groupDragItems.at(i);
        if (!item || !m_items.contains(item) || item->scene() != m_scene) {
            m_groupDragItems.removeAt(i);
            m_groupDragStartStates.removeAt(i);
        }
    }
    if (m_groupDragItems.isEmpty()) {
        return;
    }

    const QPointF centre = m_groupCenterStart;
    // Angle from group centre to pointer; seed from first press stored in
    // m_groupPressScenePos when the drag starts (set in mouse path).
    const QPointF v0 = m_groupPressScenePos - centre;
    const QPointF v1 = scenePos - centre;
    if (QLineF(QPointF(0, 0), v0).length() < 1e-3) {
        return;
    }
    qreal delta = qRadiansToDegrees(qAtan2(v1.y(), v1.x()) - qAtan2(v0.y(), v0.x()));
    // Match single-item / crop rotate: Shift → 15°, Ctrl → 45° (incl. 90°).
    if (mods & Qt::ShiftModifier) {
        delta = qRound(delta / 15.0) * 15.0;
    } else if (mods & Qt::ControlModifier) {
        delta = qRound(delta / 45.0) * 45.0;
    }
    const qreal rad = qDegreesToRadians(delta);
    const qreal c = qCos(rad);
    const qreal s = qSin(rad);

    for (int i = 0; i < m_groupDragItems.size(); ++i) {
        ImageItem *item = m_groupDragItems.at(i);
        const WorkspaceItemState &st = m_groupDragStartStates.at(i);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        // Orbit position around group centre; add the same delta to placement angle.
        const QPointF rel = st.pos - centre;
        const QPointF newPos(centre.x() + rel.x() * c - rel.y() * s,
                             centre.y() + rel.x() * s + rel.y() * c);
        if (!qIsFinite(newPos.x()) || !qIsFinite(newPos.y())) {
            continue;
        }
        item->setPos(newPos);
        item->setItemRotation(st.rotation + delta);
    }
    m_fitMode = false;
    emit statusChanged();
}
