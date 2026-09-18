// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pageguidegeometry.h"
#include "viewtransform.h"

#include <QLineF>
#include <QtMath>
#include <cmath>

namespace PageGuideGeometry {

void handlePoints(const QRect &viewRect, QPointF out[8])
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

int handleIndexAt(const QPoint &viewPos, const QRect &viewRect, qreal hitPx)
{
    if (!viewRect.isValid() || viewRect.isEmpty()) {
        return -1;
    }
    QPointF corners[8];
    handlePoints(viewRect, corners);
    for (int i = 0; i < 8; ++i) {
        if (QLineF(QPointF(viewPos), corners[i]).length() <= hitPx) {
            return i;
        }
    }
    return -1;
}

QRectF rectFromHandleDrag(const QPointF &scenePos, const QRectF &startRect, int handle,
                          bool fromCenter, bool lockAspect, qreal minSide)
{
    const QRectF r = startRect;
    const int h = handle;
    const bool corner = (h == 0 || h == 2 || h == 4 || h == 6);
    const QPointF c = r.center();

    qreal left = r.left();
    qreal top = r.top();
    qreal right = r.right();
    qreal bottom = r.bottom();

    if (fromCenter) {
        const qreal halfW = qAbs(scenePos.x() - c.x());
        const qreal halfH = qAbs(scenePos.y() - c.y());
        switch (h) {
        case 0: case 2: case 4: case 6:
            left = c.x() - halfW;
            right = c.x() + halfW;
            top = c.y() - halfH;
            bottom = c.y() + halfH;
            break;
        case 1: case 5:
            top = c.y() - halfH;
            bottom = c.y() + halfH;
            break;
        case 3: case 7:
            left = c.x() - halfW;
            right = c.x() + halfW;
            break;
        default:
            break;
        }
    } else {
        switch (h) {
        case 0:
            left = scenePos.x();
            top = scenePos.y();
            break;
        case 1:
            top = scenePos.y();
            break;
        case 2:
            right = scenePos.x();
            top = scenePos.y();
            break;
        case 3:
            right = scenePos.x();
            break;
        case 4:
            right = scenePos.x();
            bottom = scenePos.y();
            break;
        case 5:
            bottom = scenePos.y();
            break;
        case 6:
            left = scenePos.x();
            bottom = scenePos.y();
            break;
        case 7:
            left = scenePos.x();
            break;
        default:
            break;
        }
    }

    if (lockAspect && corner) {
        const qreal aspect = r.width() / ViewTransform::safeDivisor(r.height());
        qreal w = right - left;
        qreal hh = bottom - top;
        if (qAbs(w) / ViewTransform::safeDivisor(qAbs(hh)) > aspect) {
            const qreal newH = qAbs(w) / aspect;
            if (fromCenter) {
                top = c.y() - newH * 0.5;
                bottom = c.y() + newH * 0.5;
            } else if (h == 0 || h == 2) {
                top = bottom - std::copysign(newH, bottom - top);
            } else {
                bottom = top + std::copysign(newH, bottom - top);
            }
        } else {
            const qreal newW = qAbs(hh) * aspect;
            if (fromCenter) {
                left = c.x() - newW * 0.5;
                right = c.x() + newW * 0.5;
            } else if (h == 0 || h == 6) {
                left = right - std::copysign(newW, right - left);
            } else {
                right = left + std::copysign(newW, right - left);
            }
        }
    }

    QRectF next(QPointF(left, top), QPointF(right, bottom));
    next = next.normalized();
    if (next.width() < minSide) {
        if (fromCenter) {
            next = QRectF(c.x() - minSide * 0.5, next.top(), minSide, next.height());
        } else if (h == 0 || h == 6 || h == 7) {
            next.setLeft(next.right() - minSide);
        } else {
            next.setWidth(minSide);
        }
    }
    if (next.height() < minSide) {
        if (fromCenter) {
            next = QRectF(next.left(), c.y() - minSide * 0.5, next.width(), minSide);
        } else if (h == 0 || h == 1 || h == 2) {
            next.setTop(next.bottom() - minSide);
        } else {
            next.setHeight(minSide);
        }
    }
    return next;
}

} // namespace PageGuideGeometry
