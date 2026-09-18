// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "grouptransformgeometry.h"

#include <QLineF>
#include <QtMath>

namespace GroupTransformGeometry {

void scaleHandlePoints(const QRect &viewRect, QPointF out[8])
{
    out[0] = viewRect.topLeft();
    out[1] = QPointF(viewRect.center().x(), viewRect.top());
    out[2] = viewRect.topRight();
    out[3] = QPointF(viewRect.right(), viewRect.center().y());
    out[4] = viewRect.bottomRight();
    out[5] = QPointF(viewRect.center().x(), viewRect.bottom());
    out[6] = viewRect.bottomLeft();
    out[7] = QPointF(viewRect.left(), viewRect.center().y());
}

void rotateHandlePoints(const QRect &viewRect, QPointF out[4], qreal offsetPx)
{
    out[0] = QPointF(viewRect.center().x(), viewRect.top() - offsetPx);
    out[1] = QPointF(viewRect.right() + offsetPx, viewRect.center().y());
    out[2] = QPointF(viewRect.center().x(), viewRect.bottom() + offsetPx);
    out[3] = QPointF(viewRect.left() - offsetPx, viewRect.center().y());
}

int handleIndexAt(const QPoint &viewPos, const QRect &viewRect, qreal scaleHitPx,
                  qreal rotateHitPx, qreal rotateOffsetPx)
{
    if (!viewRect.isValid() || viewRect.isEmpty()) {
        return -1;
    }
    QPointF rot[4];
    rotateHandlePoints(viewRect, rot, rotateOffsetPx);
    for (int i = 0; i < 4; ++i) {
        if (QLineF(QPointF(viewPos), rot[i]).length() <= rotateHitPx) {
            return 8 + i;
        }
    }
    QPointF corners[8];
    scaleHandlePoints(viewRect, corners);
    for (int i = 0; i < 8; ++i) {
        if (QLineF(QPointF(viewPos), corners[i]).length() <= scaleHitPx) {
            return i;
        }
    }
    return -1;
}

QPointF scaleAnchor(const QRectF &bounds, int handle)
{
    const QRectF b = bounds;
    switch (handle) {
    case 0:
        return b.bottomRight();
    case 1:
        return QPointF(b.center().x(), b.bottom());
    case 2:
        return b.bottomLeft();
    case 3:
        return QPointF(b.left(), b.center().y());
    case 4:
        return b.topLeft();
    case 5:
        return QPointF(b.center().x(), b.top());
    case 6:
        return b.topRight();
    case 7:
        return QPointF(b.right(), b.center().y());
    default:
        return b.center();
    }
}

ScaleFactors scaleFactorsFromDrag(const QPointF &scenePos, const QRectF &boundsStart,
                                  int handle, bool shift)
{
    ScaleFactors out;
    const QRectF b = boundsStart;
    if (!b.isValid() || b.isEmpty() || handle < 0 || handle > 7) {
        return out;
    }
    const QPointF anchor = scaleAnchor(b, handle);
    qreal sx = 1.0;
    qreal sy = 1.0;
    const qreal eps = 1.0;
    const bool edgeHandle = (handle == 1 || handle == 3 || handle == 5 || handle == 7);
    const bool cornerHandle = (handle == 0 || handle == 2 || handle == 4 || handle == 6);

    switch (handle) {
    case 0:
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 1:
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        sx = 1.0;
        break;
    case 2:
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (anchor.y() - scenePos.y()) / qMax(eps, anchor.y() - b.top());
        break;
    case 3:
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = 1.0;
        break;
    case 4:
        sx = (scenePos.x() - anchor.x()) / qMax(eps, b.right() - anchor.x());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 5:
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        sx = 1.0;
        break;
    case 6:
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = (scenePos.y() - anchor.y()) / qMax(eps, b.bottom() - anchor.y());
        break;
    case 7:
        sx = (anchor.x() - scenePos.x()) / qMax(eps, anchor.x() - b.left());
        sy = 1.0;
        break;
    default:
        break;
    }

    if (edgeHandle && shift) {
        const qreal s = (handle == 1 || handle == 5) ? sy : sx;
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
        return out;
    }
    out.sx = sx;
    out.sy = sy;
    out.anchor = anchor;
    out.valid = true;
    return out;
}

} // namespace GroupTransformGeometry
