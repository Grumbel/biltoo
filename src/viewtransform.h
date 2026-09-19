// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWTRANSFORM_H
#define VIEWTRANSFORM_H

#include <QPoint>
#include <QPointF>
#include <QSize>
#include <QRect>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <QtGlobal>
#include <cmath>

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

} // namespace ViewTransform

#endif // VIEWTRANSFORM_H
