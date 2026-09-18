// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef STACKGEOMETRY_H
#define STACKGEOMETRY_H

#include <QPolygonF>
#include <QRectF>
#include <QtGlobal>

/**
 * Pure scene-space overlap tests and z-order step math for workspace
 * raise/lower stacking. Callers supply content AABB / polygons and stackZ.
 */
namespace StackGeometry {

/**
 * True when two content frames overlap in scene space.
 * Prefers AABB intersection; falls back to polygon intersection and
 * centre-in-polygon when AABBs miss partial overlaps of rotated frames.
 */
bool contentOverlaps(const QRectF &aabbA, const QPolygonF &polyA,
                     const QRectF &aabbB, const QPolygonF &polyB);

/**
 * New stackZ values after a one-step raise of @p selfZ against the next
 * higher overlapping neighbour @p aboveZ (layer sorted bottom → top).
 *
 * Sparse z: swap the two values so intermediates are not skipped.
 * Equal z: self becomes above+1 (neighbour unchanged).
 */
struct ZStep {
    qreal selfZ = 0.0;
    qreal neighbourZ = 0.0;
    /** False when neighbour z is unchanged (equal-z path). */
    bool neighbourChanges = false;
};

ZStep raiseStep(qreal selfZ, qreal aboveZ);
ZStep lowerStep(qreal selfZ, qreal belowZ);

/**
 * Index of the next higher item in a bottom→top layer, or -1 if already top.
 * @p seedIndex is the subject's position in the sorted layer.
 */
inline int raiseTargetIndex(int seedIndex, int layerCount)
{
    if (seedIndex < 0 || seedIndex + 1 >= layerCount) {
        return -1;
    }
    return seedIndex + 1;
}

/** Index of the next lower item, or -1 if already bottom. */
inline int lowerTargetIndex(int seedIndex, int layerCount)
{
    if (seedIndex <= 0 || seedIndex >= layerCount) {
        return -1;
    }
    return seedIndex - 1;
}

} // namespace StackGeometry

#endif // STACKGEOMETRY_H
