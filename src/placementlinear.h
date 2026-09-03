// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLACEMENTLINEAR_H
#define PLACEMENTLINEAR_H

#include <QTransform>

/**
 * Workspace linear pose helpers: R(θ)·H(k)·S(sx,sy) with
 * H = [[1,k],[0,1]] (horizontal shear in local space before rotation).
 * Matches ImageItem::applyLocalTransform.
 */
namespace PlacementLinear {

/** Build the linear QTransform (no translation). */
QTransform make(qreal scaleX, qreal scaleY, qreal shear, qreal rotationDeg);

/**
 * Decompose a linear transform back to R·H·S parameters.
 * @return false if the matrix is degenerate or non-finite.
 */
bool decompose(const QTransform &lin,
               qreal *scaleX, qreal *scaleY,
               qreal *shear, qreal *rotationDeg);

} // namespace PlacementLinear

#endif // PLACEMENTLINEAR_H
