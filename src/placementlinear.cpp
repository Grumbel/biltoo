// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placementlinear.h"

#include <QtMath>

namespace PlacementLinear {

/*
 * Linear pose matches ImageItem historical composition:
 *   QTransform t; t.rotate(θ); t.shear(k, 0); t.scale(sx, sy);
 * Qt right-multiplies → L = R·H·S in Qt's matrices.
 *
 * Decompose from mapped unit axes so we follow the installed Qt convention.
 */

QTransform make(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg)
{
    QTransform t;
    t.rotate(rotationDeg);
    t.shear(shear, 0.0);
    t.scale(scaleX, scaleY);
    return t;
}

bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg)
{
    if (!scaleX || !scaleY || !shear || !rotationDeg) {
        return false;
    }
    const QPointF e1 = lin.map(QPointF(1.0, 0.0)) - lin.map(QPointF(0.0, 0.0));
    const QPointF e2 = lin.map(QPointF(0.0, 1.0)) - lin.map(QPointF(0.0, 0.0));
    const qreal sx = qHypot(e1.x(), e1.y());
    if (sx < 1e-9 || !qIsFinite(sx)) {
        return false;
    }
    const qreal cosT = e1.x() / sx;
    const qreal sinT = e1.y() / sx;
    const qreal rot = qRadiansToDegrees(qAtan2(sinT, cosT));
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

} // namespace PlacementLinear
