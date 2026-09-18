// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWTRANSFORM_H
#define VIEWTRANSFORM_H

#include <QPoint>
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

/** Viewport size with each axis at least 1 (avoids divide-by-zero). */
inline QSize atLeast1(const QSize &s)
{
    return QSize(qMax(1, s.width()), qMax(1, s.height()));
}

inline int atLeast1(int v) { return qMax(1, v); }

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

} // namespace ViewTransform

#endif // VIEWTRANSFORM_H
