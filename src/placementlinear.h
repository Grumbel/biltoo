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

/** Angle in degrees from @p centre to @p scenePos (atan2). */
qreal angleAbout(const QPointF &centre, const QPointF &scenePos);

/** Snap @p degrees to the nearest multiple of @p stepDegrees. */
qreal snapDegrees(qreal degrees, qreal stepDegrees);

/**
 * Image-mode content orientation: nearest 90° in [0, 360). Free residual is
 * discarded (never shown as an arbitrary angle).
 */
qreal cardinalRotationOrZero(qreal degrees);

/**
 * Live placement rotation from pointer drag about item centre.
 * Ctrl → 90° snap; else Shift → 45° (workspace free-rotate holds Shift to start).
 */
qreal placementRotationFromDrag(qreal startRotation, qreal startAngleDeg,
                                qreal currentAngleDeg, bool snap90, bool snap45);

/**
 * Free-rotate handle style: Shift → 15°, else Ctrl → 45° (item chrome / crop / group).
 * Prefer snap15 when both modifiers are set.
 */
qreal freeRotationFromDrag(qreal startRotation, qreal startAngleDeg,
                           qreal currentAngleDeg, bool snap15, bool snap45);

} // namespace PlacementLinear

#endif // PLACEMENTLINEAR_H
