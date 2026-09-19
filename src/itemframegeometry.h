// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMFRAMEGEOMETRY_H
#define ITEMFRAMEGEOMETRY_H

#include <QPointF>

/**
 * Pure viewport-space geometry of a (possibly rotated) content frame:
 * corners, edge mids, unit edge directions, outward normals, and chrome
 * column / opacity-track layout. No item or view state.
 */
namespace ItemFrameGeometry {

/** Scale/rotate markers in viewport px (grow on hover). */
constexpr qreal kHandleScreenPx = 16.0;
/** Content-edit marks (crop/orient/grade) in viewport px. */
constexpr qreal kContentEditMarkScreenPx = 20.0;
/** Free-rotate handle distance from edge (viewport px). */
constexpr qreal kRotateOffsetPx = 36.0;
/** Chrome button diameter (viewport px). */
constexpr qreal kChromeBtnScreenPx = 34.0;
/** Chrome button hit radius (viewport px). */
constexpr qreal kChromeHitScreenPx = 28.0;
/** Outside offset from visual right edge to button column centre. */
constexpr qreal kChromeOutsidePx = 18.0;
/** Gap within a chrome button group. */
constexpr qreal kChromeBtnGapPx = 14.0;
/** Min air from chrome group edge to rotate knob. */
constexpr qreal kChromeClearPx = 16.0;
/** Extra gap between upper/lower chrome groups (rotate lives here). */
constexpr qreal kChromeGroupGapPx = 22.0;
constexpr int kChromeUpperCount = 4; // flip / flip / 90°CCW / 90°CW
constexpr int kChromeLowerCount = 5; // raise / lower / 1:1 / 0° / shear
constexpr int kChromeCount = kChromeUpperCount + kChromeLowerCount;
/** Opacity track length along the edge (viewport px). */
constexpr qreal kSliderWidthPx = 100.0;
/** Opacity track thickness (viewport px). */
constexpr qreal kSliderHeightPx = 10.0;
/** Outside offset from visual left edge to opacity track centre-line. */
constexpr qreal kSliderOutsidePx = 18.0;
/** Min air between opacity track and left scale/rotate clearance. */
constexpr qreal kSliderClearPx = 16.0;
/** Skip detailed chrome when frame diagonal is below this (viewport px). */
constexpr qreal kMinFrameDiagPx = 16.0;

struct FrameViewGeom {
    QPointF tl, tr, br, bl, center;
    QPointF midTop, midRight, midBottom, midLeft;
    QPointF dirTop, dirRight, dirBottom, dirLeft;
    QPointF outTop, outRight, outBottom, outLeft;
};

/** Unit vector of @p v, or @p fallback when near-zero length. */
QPointF unitOr(const QPointF &v, const QPointF &fallback = QPointF(1, 0));

/**
 * Build frame geometry from the four content corners in the same space
 * (typically viewport pixels after mapFromScene).
 */
FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl);

/**
 * Two chrome groups outside the rotated right edge, split around the free-
 * rotate knob. outCenters[0..3] upper (flip/90°), [4..8] lower (raise/reset).
 */
void chromeCentersView(const FrameViewGeom &g, QPointF outCenters[kChromeCount]);

/**
 * Vertical opacity track outside the left edge. a = bottom (5%), b = top (100%).
 * Track length is always kSliderWidthPx; may extend past the frame when cramped.
 */
void opacityTrackView(const FrameViewGeom &g, QPointF *aOut, QPointF *bOut);

/**
 * Parameter t ∈ [0, 1] along segment a→b for pointer @p p (clamped projection).
 * Degenerate segment → 0.
 */
qreal trackParam(const QPointF &a, const QPointF &b, const QPointF &p);

/** Opacity at track bottom end (matches PlacementLinear::clampOpacity lo). */
constexpr qreal kOpacityTrackMin = 0.05;
/** Opacity at track top end. */
constexpr qreal kOpacityTrackMax = 1.0;
/** Track span (max − min) for t ↔ opacity. */
constexpr qreal kOpacityTrackSpan = kOpacityTrackMax - kOpacityTrackMin;

/** Map track parameter t to item opacity (5% … 100%). */
qreal opacityFromTrackParam(qreal t);

/** Inverse of opacityFromTrackParam: opacity → t ∈ [0,1]. */
qreal trackParamFromOpacity(qreal opacity);

/** Closest point on segment a→b to @p p (uses trackParam). */
QPointF closestPointOnSegment(const QPointF &a, const QPointF &b, const QPointF &p);

/** Distance from @p p to segment a→b. */
qreal distanceToSegment(const QPointF &a, const QPointF &b, const QPointF &p);

/** Four free-rotate knob centres outside the frame (T/R/B/L). */
void rotateHandlePoints(const FrameViewGeom &g, QPointF out[4],
                        qreal offsetPx = kRotateOffsetPx);

/**
 * Shear diamond centres along edges (T/B/L/R order matching ShearTop…ShearRight).
 * Offset is distance along the edge from the mid in viewport px.
 */
void shearHandlePoints(const FrameViewGeom &g, QPointF out[4],
                       qreal alongPx = kHandleScreenPx * 2.2);

/** Placeholder “⋯” point size from content inner edge (scene-ish units). */
inline qreal placeholderEllipsisPointSize(qreal innerEdge)
{
    return qBound(8.0, innerEdge * 0.08, 28.0);
}

/** Content-edit mark glyph size from half-box edge. */
inline qreal contentEditGlyphPointSize(qreal halfEdge)
{
    return qMax(8.0, halfEdge * 0.72);
}

/** Chrome button glyph size from button radius (viewport px). */
inline qreal chromeGlyphPointSize(qreal buttonRadius)
{
    return qMax(7.0, buttonRadius * 0.55);
}

/** Neutral placeholder inset as fraction of content short edge. */
inline qreal placeholderInset(qreal contentW, qreal contentH, qreal frac = 0.06)
{
    return qMin(contentW, contentH) * frac;
}

} // namespace ItemFrameGeometry

#endif // ITEMFRAMEGEOMETRY_H
