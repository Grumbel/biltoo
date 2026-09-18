// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cropgeometry.h"

#include <QTransform>
#include <QtMath>

namespace CropGeometry {

QPolygonF rotatedCorners(const QRectF &rect, qreal degrees)
{
    const QPointF c = rect.center();
    QTransform tr;
    tr.translate(c.x(), c.y());
    tr.rotate(degrees);
    tr.translate(-c.x(), -c.y());
    QPolygonF poly;
    poly << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
    return tr.map(poly);
}

bool pointInsideBounds(const QPointF &p, const QRectF &bounds)
{
    return p.x() >= bounds.left() && p.x() <= bounds.right()
        && p.y() >= bounds.top() && p.y() <= bounds.bottom();
}

bool cornersInside(const QRectF &rect, qreal degrees, const QRectF &bounds)
{
    const QPolygonF poly = rotatedCorners(rect, degrees);
    for (const QPointF &pt : poly) {
        if (!pointInsideBounds(pt, bounds)) {
            return false;
        }
    }
    return true;
}

QRectF translateInside(QRectF rect, qreal degrees, const QRectF &bounds)
{
    for (int pass = 0; pass < 6; ++pass) {
        const QPolygonF poly = rotatedCorners(rect, degrees);
        qreal dx = 0.0;
        qreal dy = 0.0;
        for (const QPointF &pt : poly) {
            if (pt.x() < bounds.left()) {
                dx = qMax(dx, bounds.left() - pt.x());
            } else if (pt.x() > bounds.right()) {
                dx = qMin(dx, bounds.right() - pt.x());
            }
            if (pt.y() < bounds.top()) {
                dy = qMax(dy, bounds.top() - pt.y());
            } else if (pt.y() > bounds.bottom()) {
                dy = qMin(dy, bounds.bottom() - pt.y());
            }
        }
        if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy)) {
            break;
        }
        rect.translate(dx, dy);
    }
    return rect;
}

QRectF shrinkInside(QRectF rect, qreal degrees, const QRectF &bounds, qreal minSide)
{
    if (cornersInside(rect, degrees, bounds)) {
        return rect;
    }
    const QPointF c = rect.center();
    qreal lo = 0.0;
    qreal hi = 1.0;
    QRectF best(c.x() - minSide / 2.0, c.y() - minSide / 2.0, minSide, minSide);
    for (int i = 0; i < 18; ++i) {
        const qreal mid = (lo + hi) * 0.5;
        QRectF r(0, 0, rect.width() * mid, rect.height() * mid);
        r.moveCenter(c);
        if (r.width() < minSide || r.height() < minSide) {
            hi = mid;
            continue;
        }
        if (cornersInside(r, degrees, bounds)) {
            best = r;
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return best;
}

QRectF constrainToContent(QRectF rect, qreal degrees, const QRectF &bounds,
                          qreal minSide)
{
    rect = rect.normalized();
    if (rect.width() < minSide) {
        rect.setWidth(minSide);
    }
    if (rect.height() < minSide) {
        rect.setHeight(minSide);
    }
    if (qAbs(degrees) < 0.05) {
        rect = rect.intersected(bounds);
        if (rect.width() < minSide) {
            rect.setWidth(minSide);
        }
        if (rect.height() < minSide) {
            rect.setHeight(minSide);
        }
        if (rect.right() > bounds.right()) {
            rect.moveRight(bounds.right());
        }
        if (rect.bottom() > bounds.bottom()) {
            rect.moveBottom(bounds.bottom());
        }
        if (rect.left() < bounds.left()) {
            rect.moveLeft(bounds.left());
        }
        if (rect.top() < bounds.top()) {
            rect.moveTop(bounds.top());
        }
        return rect;
    }
    rect = translateInside(rect, degrees, bounds);
    if (!cornersInside(rect, degrees, bounds)) {
        rect = shrinkInside(rect, degrees, bounds, minSide);
        rect = translateInside(rect, degrees, bounds);
    }
    return rect;
}

} // namespace CropGeometry
