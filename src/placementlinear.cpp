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

bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg)
{
    if (!scaleX || !scaleY || !shear || !rotationDeg) {
        return false;
    }
    const qreal a = lin.m11();
    const qreal b = lin.m12();
    const qreal c = lin.m21();
    const qreal d = lin.m22();
    const qreal sx = qHypot(a, c);
    if (sx < 1e-9 || !qIsFinite(sx)) {
        return false;
    }
    const qreal cosT = a / sx;
    const qreal sinT = c / sx;
    const qreal rot = qRadiansToDegrees(qAtan2(sinT, cosT));
    // R^T * col2 = (sy*k, sy)
    const qreal rtx = cosT * b + sinT * d;
    const qreal rty = -sinT * b + cosT * d;
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
