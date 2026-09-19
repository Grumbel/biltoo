// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPGEOMETRY_H
#define CROPGEOMETRY_H

#include "crophandle.h"

#include <QPoint>
#include <QPolygonF>
#include <QRect>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QtGlobal>

/**
 * Pure crop geometry and viewport chrome layout / hit-test.
 * No ImageView state — safe to unit-test and share with paint and input.
 */
namespace CropGeometry {

/** Chrome button visual role (Expand toggle, Auto/Reset, Cancel, Apply). */
enum class CropBtnRole { Toggle, Action, Neutral, Commit };


/** Crop chrome button size (viewport CSS px). */
constexpr int kChromeBtnW = 70;
constexpr int kChromeBtnH = 28;
constexpr int kChromeBtnGap = 6;
/** Air below frame before buttons when placed outside. */
constexpr int kChromeOutsideGap = 32;
/** Inset from crop bottom when buttons fit inside. */
constexpr int kChromeInsideInset = 10;
/** Viewport edge margin for button clamp. */
constexpr int kChromeMargin = 6;
/** Min air between left (Expand/Auto) and right (Reset/Cancel/Apply) groups. */
constexpr int kChromeGroupGapMin = 18;

/** Degrees below this are treated as axis-aligned crop. */
constexpr qreal kFreeRotationEps = 0.05;

inline int clampLabelPointSize(int basePt)
{
    return qMax(9, basePt);
}

inline int clampButtonPointSize(int basePt)
{
    return qMax(9, basePt + 1);
}

/** Four corners of @p rect after rotation about its centre by @p degrees. */
QPolygonF rotatedCorners(const QRectF &rect, qreal degrees);

bool pointInsideBounds(const QPointF &p, const QRectF &bounds);

/** True when every rotated corner of @p rect lies inside @p bounds. */
bool cornersInside(const QRectF &rect, qreal degrees, const QRectF &bounds);

/** Translate @p rect so rotated corners fit inside @p bounds (iterative). */
QRectF translateInside(QRectF rect, qreal degrees, const QRectF &bounds);

/**
 * Binary-search scale-about-centre until corners fit, not smaller than @p minSide.
 */
QRectF shrinkInside(QRectF rect, qreal degrees, const QRectF &bounds, qreal minSide);

/**
 * Clamp draft crop to content bounds. Axis-aligned path intersects; rotated path
 * translates then shrinks if needed.
 */
QRectF constrainToContent(QRectF rect, qreal degrees, const QRectF &bounds,
                          qreal minSide);

/** True when @p rect (axis-aligned) extends outside @p bounds on any edge. */
bool axisAlignedOutside(const QRectF &rect, const QRectF &bounds);

/**
 * Expand toggle for re-entering crop on a prior draft: true when the stored
 * axis-aligned crop overflows image bounds, or a non-trivial rotation has any
 * corner outside content bounds (same thresholds as ensureCropRectValid).
 *
 * @p priorInImage / @p imageBounds are in post-bake image pixel space (0,0…size).
 * @p draftLocal / @p contentRect are item-local (offset applied).
 */
bool priorDraftNeedsExpand(const QRectF &priorInImage, const QRectF &imageBounds,
                           const QRectF &draftLocal, qreal rotationDeg,
                           const QRectF &contentRect);

/**
 * Viewport-pixel positions for crop chrome buttons under the draft frame.
 * Expand+Auto left-aligned; Reset/Cancel/Apply right-aligned; y clears rotate knobs.
 */
struct CropButtonLayout {
    QRect expand;
    QRect autoBtn;
    QRect reset;
    QRect cancel;
    QRect apply;
    bool valid = false;
};

/**
 * Place crop chrome buttons relative to @p cropView (viewport coords) and
 * clamp into @p viewportRect. Pure layout — no mode / handle state.
 */
CropButtonLayout cropButtonLayout(const QRectF &cropView, const QRect &viewportRect);

/**
 * Viewport-space anchors for a (possibly rotated) crop frame polygon.
 * Corners order: top-left, top-right, bottom-right, bottom-left.
 * Rotate knobs sit @p rotateOutset px outward from edge midpoints.
 */
struct CropFrameViewAnchors {
    QPointF tl;
    QPointF tr;
    QPointF br;
    QPointF bl;
    QPointF tm;
    QPointF bm;
    QPointF lm;
    QPointF rm;
    QPointF centre;
    QPointF rotTop;
    QPointF rotRight;
    QPointF rotBottom;
    QPointF rotLeft;
    bool valid = false;
};

/** Build anchors from a 4-point view polygon (same order as rotatedCorners). */
CropFrameViewAnchors frameViewAnchors(const QPolygonF &viewPoly,
                                      qreal rotateOutset = 22.0);

/**
 * Hit-test crop chrome in viewport pixels. Button rects first, then rotate
 * knobs, corners, edges, centre move grip. Interior returns None (rubber-band).
 */
CropHandle hitTestCropChrome(const QPoint &viewPos, const CropButtonLayout &buttons,
                             const CropFrameViewAnchors &anchors,
                             qreal handleHitPx = 16.0, qreal moveHitPx = 12.0);

/**
 * Resize draft rect in crop-local axes, then map the new centre through
 * @p rotationDeg so grips stay under the cursor when the frame is rotated.
 *
 * @p fromCenter (Ctrl) grows symmetrically; @p forceSquare (Shift) equalizes sides.
 * Does not clamp to content — caller applies constrainToContent / expand limits.
 */
QRectF resizeDraftRect(CropHandle handle, const QPointF &local,
                       const QRectF &dragStartRect, qreal rotationDeg,
                       qreal minSide, bool fromCenter, bool forceSquare);

/** Wrap degrees into (-180, 180]. */
qreal normalizeRotationDeg(qreal degrees);

/**
 * Optional snap: Shift → 15°, else Ctrl → 45° (includes 90°). Prefer snap15 when both.
 */
qreal snapRotationDeg(qreal degrees, bool snap15, bool snap45);

/**
 * Live crop rotation from pointer: atan2 delta from drag start, normalized,
 * then snapped. @p centre is the draft rect centre in the same space as @p local.
 */
qreal rotationFromDrag(const QPointF &local, const QPointF &centre,
                       qreal rotateStartRotation, qreal rotateStartAngle,
                       bool snap15, bool snap45);

/**
 * Axis-aligned rubber-band draft from press @p origin to @p local.
 * @p forceSquare (Shift) equalizes sides from origin; @p fromCenter (Ctrl)
 * grows about origin (square when both). Does not clamp to content.
 */
QRectF rubberBandRect(const QPointF &origin, const QPointF &local,
                      bool forceSquare, bool fromCenter);

/**
 * Integer pixel crop in content-local space: round local top-left minus
 * @p offset, width/height at least 1.
 */
QRect integerCropFromLocal(const QRectF &local, const QPointF &offset);

/**
 * Map a content-local integer crop through item H/V flip into unflipped
 * source pixel space (same convention as crop bake).
 */
QRect flipAwareSourceCrop(const QRect &disp, int imageW, int imageH,
                          bool hFlip, bool vFlip);

/** Dim viewport outside the crop polygon (view coordinates). */
void paintDimOutside(QPainter &painter, const QRect &viewportRect,
                     const QPolygonF &cropViewPoly);

/** Amber crop frame outline (solid + dash). */
void paintFrame(QPainter &painter, const QPolygonF &cropViewPoly);

/** Draw a labeled crop chrome button (hover from handle hot state). */
void paintTextButton(QPainter &painter, const QRect &btn, bool hover,
                     const QString &label, CropBtnRole role, bool toggled = false);


} // namespace CropGeometry

#endif // CROPGEOMETRY_H

