// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placementlinear.h"

#include <QtMath>

namespace PlacementLinear {

/*
 * Linear pose L = R(θ)·H(k)·S(sx,sy) with
 *   H = |1 k|   S = |sx  0|   R = |c -s|   (CCW in math coords; matches
 *       |0 1|       | 0 sy|       |s  c|    mapping unit axes through L).
 *
 * Built element-wise so results do not depend on QTransform::rotate/shear/scale
 * right-multiply conventions across Qt versions.
 *
 * Columns of L (images of local basis):
 *   e1 = R·(sx, 0)       = ( c·sx,  s·sx)
 *   e2 = R·(k·sy, sy)    = ( c·k·sy - s·sy,  s·k·sy + c·sy)
 */

QTransform make(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg)
{
    const qreal th = qDegreesToRadians(rotationDeg);
    const qreal c = qCos(th);
    const qreal s = qSin(th);
    const qreal sx = scaleX;
    const qreal sy = scaleY;
    const qreal k = shear;
    // 9-parameter form (m11,m12,m13, m21,m22,m23, m31,m32,m33) — unambiguous.
    //   x' = m11*x + m12*y + m13
    //   y' = m21*x + m22*y + m23
    const qreal m11 = c * sx;
    const qreal m21 = s * sx;
    const qreal m12 = c * k * sy - s * sy;
    const qreal m22 = s * k * sy + c * sy;
    return QTransform(m11, m12, 0.0, m21, m22, 0.0, 0.0, 0.0, 1.0);
}

bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg)
{
    if (!scaleX || !scaleY || !shear || !rotationDeg) {
        return false;
    }
    const qreal a = lin.m11(); // e1.x
    const qreal b = lin.m12(); // e2.x
    const qreal c = lin.m21(); // e1.y
    const qreal d = lin.m22(); // e2.y
    const qreal sx = qHypot(a, c);
    if (sx < 1e-9 || !qIsFinite(sx)) {
        return false;
    }
    const qreal cosT = a / sx;
    const qreal sinT = c / sx;
    const qreal rot = qRadiansToDegrees(qAtan2(sinT, cosT));
    // R^T · e2 = (sy·k, sy)
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
