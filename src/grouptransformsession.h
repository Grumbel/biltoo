// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GROUPTRANSFORMSESSION_H
#define GROUPTRANSFORMSESSION_H

#include "imageview_types.h"

#include <QList>
#include <QPointF>
#include <QRectF>

class ImageItem;

/**
 * Multi-select group scale/rotate drag gesture (Workspace).
 * Handle indices: 0–7 scale, 8–11 rotate; -1 = none.
 */
class GroupTransformSession
{
public:
    void clear()
    {
        handle = -1;
        hoverHandle = -1;
        scaleDrag = false;
        rotateDrag = false;
        boundsStart = {};
        centerStart = {};
        pressScenePos = {};
        pressAngleDeg = 0.0;
        dragStartStates.clear();
        dragItems.clear();
    }

    bool active() const { return scaleDrag || rotateDrag; }

    int handle = -1;
    int hoverHandle = -1;
    bool scaleDrag = false;
    bool rotateDrag = false;
    QRectF boundsStart;
    QPointF centerStart;
    QPointF pressScenePos;
    qreal pressAngleDeg = 0.0;
    QList<WorkspaceItemState> dragStartStates;
    QList<ImageItem *> dragItems;
};

#endif // GROUPTRANSFORMSESSION_H
