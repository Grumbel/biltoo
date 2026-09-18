// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PLACEMENTLINEAR_H
#define PLACEMENTLINEAR_H

#include <QPointF>
#include <QRectF>
#include <QTransform>
#include <QtGlobal>

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

/** Bring @p degrees into [0, 360). */
qreal normalizeDegrees(qreal degrees);

/** Clamp placement scale axes to a safe interactive range. */
inline void clampScaleXY(qreal *sx, qreal *sy,
                         qreal lo = 0.01, qreal hi = 50.0)
{
    if (sx) {
        *sx = qBound(lo, *sx, hi);
    }
    if (sy) {
        *sy = qBound(lo, *sy, hi);
    }
}


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

/**
 * Uniform scale factor from two scene distances about a fixed pivot.
 * Returns 1.0 when @p d0 is below @p minDist (ignore near-degenerate samples).
 */
qreal uniformScaleFactor(qreal d0, qreal d1, qreal minDist = 1.0);

/**
 * Axis scale from projecting two vectors onto @p axis (press-time image axis).
 * Returns @p pressScale when the press projection is near zero.
 */
qreal axisScaleFromProjection(qreal pressScale, const QPointF &v0, const QPointF &v1,
                              const QPointF &axis);

/**
 * Horizontal shear kx from Top/Bottom shear-handle drag.
 * @p leverY is grip local y (sign chooses delta direction); @p len0/@p len1 are
 * projections of (pointer − anchor) onto unit local +X at press.
 */
qreal horizontalShearFromDrag(qreal pressShear, qreal pressScaleX, qreal leverY,
                              qreal len0, qreal len1);

/**
 * Vertical shear parameter m for Left/Right shear-handle drag (L·V(m)).
 * @p leverX is grip local x; @p len0/@p len1 project onto unit local +Y at press.
 */
qreal verticalShearParamFromDrag(qreal pressScaleY, qreal leverX, qreal len0, qreal len1);

/**
 * Fixed opposite corner/edge on @p content for scale/shear about that anchor.
 * Matches ImageItem scale/shear handle pairing (not from-centre mode).
 */
enum class ContentAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    TopMid,
    BottomMid,
    LeftMid,
    RightMid,
    Center,
};

QPointF contentAnchorPoint(const QRectF &content, ContentAnchor anchor);

/**
 * Singular values of the 2×2 linear map [[a,b],[c,d]] (sqrt eigenvalues of MᵀM).
 * @p sMin is clamped to at least 1e-6.
 */
void singularValues2x2(qreal a, qreal b, qreal c, qreal d, qreal *sMax, qreal *sMin);

} // namespace PlacementLinear

#endif // PLACEMENTLINEAR_H
