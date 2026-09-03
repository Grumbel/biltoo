// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placementlinear.h"

#include <QtMath>

namespace PlacementLinear {

QTransform make(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg)
{
    QTransform t;
    t.rotate(rotationDeg);
    t.shear(shear, 0.0);
    t.scale(scaleX, scaleY);
    return t;
}

bool decomposeAxes(const QPointF &e1, const QPointF &e2,
                   qreal *scaleX, qreal *scaleY,
                   qreal *shear, qreal *rotationDeg)
{
    if (!scaleX || !scaleY || !shear || !rotationDeg) {
        return false;
    }
    const qreal sx = qHypot(e1.x(), e1.y());
    if (sx < 1e-9 || !qIsFinite(sx)) {
        return false;
    }
    const qreal cosT = e1.x() / sx;
    const qreal sinT = e1.y() / sx;
    const qreal rot = qRadiansToDegrees(qAtan2(sinT, cosT));
    // In the orthonormal frame of e1: e2 → (sy·k, sy) for H=[[1,k],[0,1]].
    const qreal rtx = cosT * e2.x() + sinT * e2.y();
    const qreal rty = -sinT * e2.x() + cosT * e2.y();
    if (!qIsFinite(rtx) || !qIsFinite(rty) || qAbs(rty) < 1e-9) {
        return false;
    }
    const qreal sy = rty;
    const qreal k = rtx / sy;
    if (!qIsFinite(sy) || !qIsFinite(k)) {
        return false;
    }
    *scaleX = qAbs(sx);
    *scaleY = qAbs(sy);
    *shear = qBound(-5.0, k, 5.0);
    *rotationDeg = rot;
    return true;
}

bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg)
{
    const QPointF o = lin.map(QPointF(0.0, 0.0));
    const QPointF e1 = lin.map(QPointF(1.0, 0.0)) - o;
    const QPointF e2 = lin.map(QPointF(0.0, 1.0)) - o;
    return decomposeAxes(e1, e2, scaleX, scaleY, shear, rotationDeg);
}

void unitAxes(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg,
              QPointF *e1, QPointF *e2)
{
    const QTransform L = make(scaleX, scaleY, shear, rotationDeg);
    if (e1) {
        *e1 = L.map(QPointF(1.0, 0.0));
    }
    if (e2) {
        *e2 = L.map(QPointF(0.0, 1.0));
    }
}

} // namespace PlacementLinear
