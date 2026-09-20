// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMCOMPONENTS_H
#define ITEMCOMPONENTS_H

#include "imageview_types.h"

#include <QRect>
#include <QSize>
#include <QVector>
#include <QPointF>

/**
 * Phase 7 Stage 1 — sparse component records extracted from the fat
 * WorkspaceItemState DTO (project file / undo still use the full struct).
 *
 * Presence replaces hasCrop / hasAttention bools. Pure extract/apply helpers
 * keep the DTO and runtime tables synchronized until Stage 4 persistence split.
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

inline void applyCropToState(WorkspaceItemState &s, const Crop &c)
{
    if (c.isEmpty()) {
        s.hasCrop = false;
        s.cropRect = QRect();
        s.cropSourceSize = QSize();
        s.cropRotation = 0.0;
        return;
    }
    s.hasCrop = true;
    s.cropRect = c.rect;
    s.cropSourceSize = c.sourceSize;
    s.cropRotation = c.rotation;
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

inline void applyAttentionToState(WorkspaceItemState &s, const Attention &a)
{
    if (a.isEmpty()) {
        s.hasAttention = false;
        s.attentionPoints.clear();
        s.attentionNorm = QPointF(0.5, 0.5);
        return;
    }
    s.attentionPoints = a.points;
    s.syncAttentionPrimary();
}

} // namespace ItemComponents

#endif // ITEMCOMPONENTS_H
