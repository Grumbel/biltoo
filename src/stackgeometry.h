// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef STACKGEOMETRY_H
#define STACKGEOMETRY_H

#include <QPolygonF>
#include <QRectF>

/**
 * Pure scene-space overlap tests for workspace raise/lower stacking.
 * Callers supply content AABB and (optional) rotated content polygons.
 */
namespace StackGeometry {

/**
 * True when two content frames overlap in scene space.
 * Prefers AABB intersection; falls back to polygon intersection and
 * centre-in-polygon when AABBs miss partial overlaps of rotated frames.
 */
bool contentOverlaps(const QRectF &aabbA, const QPolygonF &polyA,
                     const QRectF &aabbB, const QPolygonF &polyB);

} // namespace StackGeometry

#endif // STACKGEOMETRY_H
