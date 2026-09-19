// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWTRANSFORM_H
#define VIEWTRANSFORM_H

#include <QPoint>
#include <QPointF>
#include <QLineF>
#include <QSize>
#include <QRect>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <QtGlobal>
#include <cmath>
#include <limits>

/**
 * Pure view-matrix helpers (no QGraphicsView).
 */
namespace ViewTransform {

/** Uniform scale factor from a linear 2D transform (column 0 length). */
inline qreal scaleFrom(const QTransform &t)
{
    return std::hypot(t.m11(), t.m12());
}

/** Floor near-zero scales so pattern cell math stays finite. */
inline qreal sanitizeViewScale(qreal viewScale, qreal floor = 1e-6)
{
    return qMax(floor, viewScale);
}

inline qreal clamp01(qreal t)
{
    return qBound(0.0, t, 1.0);
}

inline qreal safeDivisor(qreal v, qreal eps = 1e-6)
{
    return qMax(eps, v);
}

inline int pairCount(int a, int b)
{
    return qMin(a, b);
}


inline int nonNegMs(int delayMs)
{
    return qMax(0, delayMs);
}

inline qint64 nonNegMs(qint64 ms)
{
    return ms < 0 ? qint64(0) : ms;
}

constexpr qreal kWheelZoomStep = 1.25;
inline qreal wheelZoomFactor(int angleDeltaY)
{
    return angleDeltaY > 0 ? kWheelZoomStep : (1.0 / kWheelZoomStep);
}

/** Viewport size with each axis at least 1 (avoids divide-by-zero). */
inline QSize atLeast1(const QSize &s)
{
    return QSize(qMax(1, s.width()), qMax(1, s.height()));
}

inline int atLeast1(int v) { return qMax(1, v); }

/** Uniform scale to fit content into a footprint (min of axis ratios). */
inline qreal uniformFitScale(qreal footW, qreal footH, qreal contentW, qreal contentH)
{
    const qreal cw = qMax(1.0, contentW);
    const qreal ch = qMax(1.0, contentH);
    return qMin(footW / cw, footH / ch);
}

inline qreal uniformFitScale(const QSizeF &foot, const QSizeF &content)
{
    return uniformFitScale(foot.width(), foot.height(), content.width(), content.height());
}

/** Axis-aligned rect of @p content size fitted into @p target, centred. */
inline QRectF fitRectCentered(const QRectF &target, const QSizeF &content)
{
    const qreal s = uniformFitScale(target.size(), content);
    const qreal tw = content.width() * s;
    const qreal th = content.height() * s;
    return QRectF(target.center().x() - tw / 2.0,
                  target.center().y() - th / 2.0, tw, th);
}


/** Position / span clamped to [0,1]. */
inline qreal unitFraction(int pos, int span)
{
    return qBound(0.0, qreal(pos) / qreal(atLeast1(span)), 1.0);
}

inline qreal unitFraction(qreal pos, qreal span)
{
    return qBound(0.0, pos / qMax(qreal(1e-9), span), 1.0);
}

/** Expand @p bounds by @p pad on each side (empty stays empty). */
inline QRectF padded(const QRectF &bounds, qreal pad)
{
    if (!bounds.isValid() || bounds.isEmpty()) {
        return bounds;
    }
    return bounds.adjusted(-pad, -pad, pad, pad);
}

/** Axis-aligned rubber from two viewport points. */
inline QRect rubberRect(const QPoint &a, const QPoint &b)
{
    return QRect(a, b).normalized();
}

/** True when rubber is large enough to act on (not a click). */
inline bool significantRubber(const QRect &r, int minPx = 8)
{
    return r.width() >= minPx && r.height() >= minPx;
}

/** Chebyshev distance (max axis delta) between scene points. */
inline qreal chebyshev(const QPointF &a, const QPointF &b)
{
    return qMax(qAbs(b.x() - a.x()), qAbs(b.y() - a.y()));
}


/** Device-pixel font size for overlays under a view scale (aims ~@p targetPx). */
inline int overlayFontPixelSize(qreal viewScale, int targetPx = 14,
                                int lo = 10, int hi = 36)
{
    viewScale = sanitizeViewScale(viewScale);
    return qBound(lo, qRound(qreal(targetPx) / viewScale), hi);
}


/** Point at parameter t ∈ [0,1] along segment a→b (t clamped). */
inline QPointF pointAlong(const QPointF &a, const QPointF &b, qreal t)
{
    t = clamp01(t);
    return a + (b - a) * t;
}


/** Integer progress for QProgressBar-style widgets (0 when total ≤ 0). */
inline int clampedProgress(int current, int total)
{
    if (total <= 0) {
        return 0;
    }
    return qBound(0, current, total);
}


/** Scale to fit content inside box (contain / Fit). Axes floored at 1. */
inline qreal containScale(qreal boxW, qreal boxH, qreal contentW, qreal contentH)
{
    const qreal cw = contentW > 1.0 ? contentW : 1.0;
    const qreal ch = contentH > 1.0 ? contentH : 1.0;
    const qreal sx = boxW / cw;
    const qreal sy = boxH / ch;
    return sx < sy ? sx : sy;
}

/** Scale to cover box (may crop / Fill). */
inline qreal coverScale(qreal boxW, qreal boxH, qreal contentW, qreal contentH)
{
    const qreal cw = contentW > 1.0 ? contentW : 1.0;
    const qreal ch = contentH > 1.0 ? contentH : 1.0;
    const qreal sx = boxW / cw;
    const qreal sy = boxH / ch;
    return sx > sy ? sx : sy;
}

/** Floor interactive view/item scale so matrices stay invertible. */
inline qreal floorScale(qreal s, qreal floor = 0.01)
{
    return qMax(floor, s);
}


/** Clamp integer index into [0, count-1]; 0 when count ≤ 0. */
inline int clampIndex(int index, int count)
{
    if (count <= 0) {
        return 0;
    }
    return qBound(0, index, count - 1);
}

/** Clamp insert position into [0, size] (size = append). */
inline int clampInsertIndex(int index, int size)
{
    if (size < 0) {
        return 0;
    }
    return qBound(0, index, size);
}

/** Clamp pixel coordinate into [0, extent-1]. */
inline int clampPixel(int coord, int extent)
{
    if (extent <= 0) {
        return 0;
    }
    return qBound(0, coord, extent - 1);
}


/** Non-negative qreal (floors at 0). */
inline qreal nonNeg(qreal v)
{
    return v < 0.0 ? 0.0 : v;
}

inline int nonNeg(int v)
{
    return v < 0 ? 0 : v;
}

/** Integral non-negative (qsizetype / qint64 counts → int, floored at 0). */
inline int nonNeg(qint64 v)
{
    if (v <= 0) {
        return 0;
    }
    if (v > qint64(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }
    return int(v);
}


/**
 * Eight axis-aligned scale-grip centres for a view rect (TL, T, TR, R, BR, B, BL, L).
 * Shared by page-guide and group-transform chrome.
 */
inline void axisAlignedHandlePoints(const QRect &viewRect, QPointF out[8])
{
    out[0] = viewRect.topLeft();
    out[1] = QPointF(viewRect.center().x(), viewRect.top());
    out[2] = viewRect.topRight();
    out[3] = QPointF(viewRect.right(), viewRect.center().y());
    out[4] = viewRect.bottomRight();
    out[5] = QPointF(viewRect.center().x(), viewRect.bottom());
    out[6] = viewRect.bottomLeft();
    out[7] = QPointF(viewRect.left(), viewRect.center().y());
}

/**
 * Index of the nearest of 8 axis-aligned grips within @p hitPx, or -1.
 * Empty/invalid rect → -1.
 */
inline int axisAlignedHandleIndexAt(const QPoint &viewPos, const QRect &viewRect,
                                    qreal hitPx)
{
    if (!viewRect.isValid() || viewRect.isEmpty() || hitPx < 0.0) {
        return -1;
    }
    QPointF pts[8];
    axisAlignedHandlePoints(viewRect, pts);
    int best = -1;
    qreal bestDist = hitPx;
    for (int i = 0; i < 8; ++i) {
        const qreal d = QLineF(QPointF(viewPos), pts[i]).length();
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

} // namespace ViewTransform

#endif // VIEWTRANSFORM_H
