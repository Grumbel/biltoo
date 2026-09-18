// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPGEOMETRY_H
#define CROPGEOMETRY_H

#include <QPolygonF>
#include <QRectF>

/**
 * Pure crop-rect geometry in item-local content space.
 * No ImageView / session state — safe to unit-test and share with undo helpers.
 */
namespace CropGeometry {

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

} // namespace CropGeometry

#endif // CROPGEOMETRY_H
