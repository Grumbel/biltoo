// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "placementlinear.h"

#include <QtMath>
#include <cmath>

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
    *shear = clampShear(k);
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

qreal angleAbout(const QPointF &centre, const QPointF &scenePos)
{
    return qRadiansToDegrees(qAtan2(scenePos.y() - centre.y(), scenePos.x() - centre.x()));
}

qreal normalizeDegrees(qreal degrees)
{
    while (degrees >= 360.0) {
        degrees -= 360.0;
    }
    while (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

qreal snapDegrees(qreal degrees, qreal stepDegrees)
{
    if (stepDegrees <= 0.0) {
        return degrees;
    }
    return qRound(degrees / stepDegrees) * stepDegrees;
}

qreal cardinalRotationOrZero(qreal degrees)
{
    const qreal n = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    qreal snapped = snapDegrees(n, 90.0);
    if (snapped >= 360.0) {
        snapped = 0.0;
    }
    return snapped;
}

qreal placementRotationFromDrag(qreal startRotation, qreal startAngleDeg,
                                qreal currentAngleDeg, bool snap90, bool snap45)
{
    qreal rot = startRotation + (currentAngleDeg - startAngleDeg);
    if (snap90) {
        rot = snapDegrees(rot, 90.0);
    } else if (snap45) {
        rot = snapDegrees(rot, 45.0);
    }
    return rot;
}

qreal freeRotationFromDrag(qreal startRotation, qreal startAngleDeg,
                           qreal currentAngleDeg, bool snap15, bool snap45)
{
    qreal rot = startRotation + (currentAngleDeg - startAngleDeg);
    if (snap15) {
        rot = snapDegrees(rot, 15.0);
    } else if (snap45) {
        rot = snapDegrees(rot, 45.0);
    }
    return rot;
}

qreal uniformScaleFactor(qreal d0, qreal d1, qreal minDist)
{
    if (d0 <= minDist) {
        return 1.0;
    }
    return d1 / d0;
}

qreal axisScaleFromProjection(qreal pressScale, const QPointF &v0, const QPointF &v1,
                              const QPointF &axis)
{
    const qreal uAxis = qMax(1e-9, QPointF::dotProduct(axis, axis));
    const qreal len0 = QPointF::dotProduct(v0, axis) / uAxis;
    const qreal len1 = QPointF::dotProduct(v1, axis) / uAxis;
    if (qAbs(len0) <= 1e-6) {
        return pressScale;
    }
    return pressScale * (qAbs(len1) / qAbs(len0));
}

qreal horizontalShearFromDrag(qreal pressShear, qreal pressScaleX, qreal leverY,
                              qreal len0, qreal len1)
{
    const qreal lever = qMax(1e-3, qAbs(leverY));
    const qreal denom = qMax(1e-6, pressScaleX * lever);
    const qreal delta = (len1 - len0) / denom;
    if (leverY < 0.0) {
        return pressShear - delta;
    }
    return pressShear + delta;
}

qreal verticalShearParamFromDrag(qreal pressScaleY, qreal leverX, qreal len0, qreal len1)
{
    const qreal lever = qMax(1e-3, qAbs(leverX));
    const qreal denom = qMax(1e-6, pressScaleY * lever);
    const qreal delta = (len1 - len0) / denom;
    if (leverX < 0.0) {
        return -delta; // left edge (x < 0)
    }
    return delta;
}

QPointF contentAnchorPoint(const QRectF &content, ContentAnchor anchor)
{
    const QRectF r = content;
    switch (anchor) {
    case ContentAnchor::TopLeft:
        return r.topLeft();
    case ContentAnchor::TopRight:
        return r.topRight();
    case ContentAnchor::BottomLeft:
        return r.bottomLeft();
    case ContentAnchor::BottomRight:
        return r.bottomRight();
    case ContentAnchor::TopMid:
        return QPointF(r.center().x(), r.top());
    case ContentAnchor::BottomMid:
        return QPointF(r.center().x(), r.bottom());
    case ContentAnchor::LeftMid:
        return QPointF(r.left(), r.center().y());
    case ContentAnchor::RightMid:
        return QPointF(r.right(), r.center().y());
    case ContentAnchor::Center:
    default:
        return r.center();
    }
}

void singularValues2x2(qreal a, qreal b, qreal c, qreal d, qreal *sMax, qreal *sMin)
{
    // Singular values of [[a,b],[c,d]] = sqrt(eigenvalues of M^T M).
    const qreal e11 = a * a + c * c;
    const qreal e22 = b * b + d * d;
    const qreal e12 = a * b + c * d;
    const qreal tr = e11 + e22;
    const qreal disc = qMax(0.0, (e11 - e22) * (e11 - e22) + 4.0 * e12 * e12);
    const qreal root = qSqrt(disc);
    const qreal ev1 = qMax(0.0, 0.5 * (tr + root));
    const qreal ev2 = qMax(0.0, 0.5 * (tr - root));
    if (sMax) {
        *sMax = qMax(qSqrt(ev1), qSqrt(ev2));
    }
    if (sMin) {
        *sMin = qMax(1e-6, qMin(qSqrt(ev1), qSqrt(ev2)));
    }
}

} // namespace PlacementLinear
