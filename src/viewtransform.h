// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWTRANSFORM_H
#define VIEWTRANSFORM_H

#include <QRectF>
#include <QTransform>
#include <QtMath>
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

/** Expand @p bounds by @p pad on each side (empty stays empty). */
inline QRectF padded(const QRectF &bounds, qreal pad)
{
    if (!bounds.isValid() || bounds.isEmpty()) {
        return bounds;
    }
    return bounds.adjusted(-pad, -pad, pad, pad);
}

} // namespace ViewTransform

#endif // VIEWTRANSFORM_H
