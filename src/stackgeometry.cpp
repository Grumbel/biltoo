// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "stackgeometry.h"

#include <QtGlobal>

namespace StackGeometry {

bool contentOverlaps(const QRectF &aabbA, const QPolygonF &polyA,
                     const QRectF &aabbB, const QPolygonF &polyB)
{
    // Prefer AABB so partial / edge overlaps still count as a stack step.
    // Polygon-only tests were missing some overlaps and made Raise jump.
    if (aabbA.intersects(aabbB)) {
        return true;
    }
    if (polyA.isEmpty() || polyB.isEmpty()) {
        return false;
    }
    if (polyA.intersects(polyB)) {
        return true;
    }
    if (polyB.containsPoint(polyA.boundingRect().center(), Qt::OddEvenFill)
        || polyA.containsPoint(polyB.boundingRect().center(), Qt::OddEvenFill)) {
        return true;
    }
    return false;
}

ZStep raiseStep(qreal selfZ, qreal aboveZ)
{
    ZStep step;
    if (qFuzzyCompare(selfZ, aboveZ)) {
        step.selfZ = aboveZ + 1.0;
        step.neighbourZ = aboveZ;
        step.neighbourChanges = false;
    } else {
        step.selfZ = aboveZ;
        step.neighbourZ = selfZ;
        step.neighbourChanges = true;
    }
    return step;
}

ZStep lowerStep(qreal selfZ, qreal belowZ)
{
    ZStep step;
    if (qFuzzyCompare(selfZ, belowZ)) {
        step.selfZ = belowZ - 1.0;
        step.neighbourZ = belowZ;
        step.neighbourChanges = false;
    } else {
        step.selfZ = belowZ;
        step.neighbourZ = selfZ;
        step.neighbourChanges = true;
    }
    return step;
}

} // namespace StackGeometry
