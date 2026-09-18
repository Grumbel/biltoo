// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMINTERACTSESSION_H
#define ITEMINTERACTSESSION_H

#include "imageview_types.h"

class ImageItem;

/**
 * Single-item Workspace interaction: move, corner handle scale, free rotate.
 * Distinct from GroupTransformSession (multi-select).
 */
struct ItemInteractSession {
    bool rotating = false;
    ImageItem *rotateItem = nullptr;
    qreal rotateStartAngle = 0.0;
    qreal rotateItemStart = 0.0;

    ImageItem *handleDragItem = nullptr;

    ImageItem *dragItem = nullptr;
    WorkspaceItemState dragStartState;

    void clearRotate()
    {
        rotating = false;
        rotateItem = nullptr;
        rotateStartAngle = 0.0;
        rotateItemStart = 0.0;
    }

    void clearHandleDrag() { handleDragItem = nullptr; }

    void clearMove()
    {
        dragItem = nullptr;
        dragStartState = {};
    }

    void clear()
    {
        clearRotate();
        clearHandleDrag();
        clearMove();
    }
};

#endif // ITEMINTERACTSESSION_H
