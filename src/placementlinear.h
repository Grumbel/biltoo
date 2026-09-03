// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLACEMENTLINEAR_H
#define PLACEMENTLINEAR_H

#include <QPointF>
#include <QTransform>

/**
 * Workspace linear pose helpers: R(θ)·H(k)·S(sx,sy) with
 * H = [[1,k],[0,1]] (horizontal shear in local space before rotation).
 * Matches ImageItem::applyLocalTransform / Qt rotate·shear·scale order.
 */
namespace PlacementLinear {

/** Build the linear QTransform (no translation). */
QTransform make(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg);

/**
 * Decompose a linear transform back to R·H·S parameters.
 * Uses mapped unit axes so it tracks Qt's matrix convention.
 */
bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg);

/**
 * Decompose from images of the local unit axes (e1 = L(1,0), e2 = L(0,1)).
 * Preferred when conjugating a scene stretch: e' = (sx*e.x, sy*e.y).
 */
bool decomposeAxes(const QPointF &e1, const QPointF &e2,
                   qreal *scaleX, qreal *scaleY,
                   qreal *shear, qreal *rotationDeg);

/** Scene-space images of local +X and +Y for a pose. */
void unitAxes(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg,
              QPointF *e1, QPointF *e2);

} // namespace PlacementLinear

#endif // PLACEMENTLINEAR_H
