// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMCOMPONENTS_H
#define ITEMCOMPONENTS_H

#include "color/coloradjust.h"
#include "imageview_types.h"

#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

/**
 * Phase 7 Stage 1 — sparse component records extracted from the fat
 * WorkspaceItemState DTO (project file / undo still use the full struct).
 *
 * Presence replaces hasCrop / hasAttention / non-identity grade / non-zero bake.
 * Pure extract/apply helpers bridge DTO ↔ sparse tables at load (setAppearance)
 * and project/clipboard assemble (appearanceValue). Stage 4b: no dual-write.
 */
namespace ItemComponents {

struct Crop {
    QRect rect;
    QSize sourceSize;
    qreal rotation = 0.0;

    bool isEmpty() const { return rect.isEmpty(); }
};

struct Attention {
    QVector<QPointF> points;

    bool isEmpty() const { return points.isEmpty(); }
};

/**
 * Workspace / free-placement pose (not content orient).
 * Presence: non-default scale/pos/rotation/shear/opacity/z/item flips.
 * Identity placement (origin, scale 1, no flips) is still stored when the
 * item is on the canvas — use hasPlacement on ItemWorld for table presence.
 */
struct Placement {
    QPointF pos;
    qreal scale = 1.0;
    qreal scaleY = 1.0;
    qreal shear = 0.0;
    qreal rotation = 0.0;
    qreal opacity = 1.0;
    qreal z = 0.0;
    bool hFlip = false;
    bool vFlip = false;

    bool isIdentity() const
    {
        return pos.isNull() && qFuzzyCompare(scale, 1.0) && qFuzzyCompare(scaleY, 1.0)
            && qFuzzyIsNull(shear) && qFuzzyIsNull(rotation)
            && qFuzzyCompare(opacity, 1.0) && qFuzzyIsNull(z)
            && !hFlip && !vFlip;
    }
};

/** True when pose is unchanged for undo no-op (move/rotate release). */
inline bool placementNearlyEqual(const Placement &a, const Placement &b)
{
    return a.pos == b.pos
        && qFuzzyCompare(a.scale, b.scale)
        && qFuzzyCompare(a.scaleY > 0.0 ? a.scaleY : 1.0, b.scaleY > 0.0 ? b.scaleY : 1.0)
        && qFuzzyCompare(a.shear + 1.0, b.shear + 1.0)
        && qFuzzyCompare(a.rotation, b.rotation)
        && qFuzzyCompare(a.opacity, b.opacity)
        && qFuzzyCompare(a.z + 1.0, b.z + 1.0)
        && a.hFlip == b.hFlip && a.vFlip == b.vFlip;
}

/** Content orient bake (disk → flip → quarter turns), independent of placement. */
struct ContentBake {
    int quarterTurns = 0; // 0..3
    bool hFlip = false;
    bool vFlip = false;

    bool isIdentity() const
    {
        return quarterTurns == 0 && !hFlip && !vFlip;
    }
};

struct Color {
    ColorAdjustments grade;

    bool isIdentity() const { return grade.isIdentity(); }
};

inline Crop cropFromState(const WorkspaceItemState &s)
{
    Crop c;
    if (!s.hasCrop || s.cropRect.isEmpty()) {
        return c;
    }
    c.rect = s.cropRect;
    c.sourceSize = s.cropSourceSize;
    c.rotation = s.cropRotation;
    return c;
}


inline Attention attentionFromState(const WorkspaceItemState &s)
{
    Attention a;
    if (!s.hasAttention && s.attentionPoints.isEmpty()) {
        return a;
    }
    a.points = s.attentionPoints;
    if (a.points.isEmpty() && s.hasAttention) {
        a.points.append(s.attentionNorm);
    }
    return a;
}


inline Placement placementFromState(const WorkspaceItemState &s)
{
    Placement p;
    p.pos = s.pos;
    p.scale = s.scale;
    p.scaleY = s.scaleY;
    p.shear = s.shear;
    p.rotation = s.rotation;
    p.opacity = s.opacity;
    p.z = s.z;
    p.hFlip = s.hFlip;
    p.vFlip = s.vFlip;
    return p;
}

inline void applyPlacementToState(WorkspaceItemState &s, const Placement &p)
{
    s.pos = p.pos;
    s.scale = p.scale;
    s.scaleY = p.scaleY;
    s.shear = p.shear;
    s.rotation = p.rotation;
    s.opacity = p.opacity;
    s.z = p.z;
    s.hFlip = p.hFlip;
    s.vFlip = p.vFlip;
}

inline ContentBake contentBakeFromState(const WorkspaceItemState &s)
{
    ContentBake b;
    int t = s.contentQuarterTurns % 4;
    if (t < 0) {
        t += 4;
    }
    b.quarterTurns = t;
    b.hFlip = s.contentHFlip;
    b.vFlip = s.contentVFlip;
    return b;
}


inline Color colorFromState(const WorkspaceItemState &s)
{
    Color c;
    c.grade = s.colorAdjust;
    return c;
}


} // namespace ItemComponents

#endif // ITEMCOMPONENTS_H
