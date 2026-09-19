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

    bool isRotating() const { return rotating && rotateItem; }

    bool isHandleDragging() const { return handleDragItem != nullptr; }

    bool isMoving() const { return dragItem != nullptr; }

    bool isActive() const
    {
        return isRotating() || isHandleDragging() || isMoving();
    }

    void beginRotate(ImageItem *item, qreal startAngle, qreal itemStart,
                     const WorkspaceItemState &startState)
    {
        rotating = true;
        rotateItem = item;
        rotateStartAngle = startAngle;
        rotateItemStart = itemStart;
        dragStartState = startState;
    }

    void endRotate() { clearRotate(); }

    void beginHandleDrag(ImageItem *item, const WorkspaceItemState &startState)
    {
        handleDragItem = item;
        dragItem = item;
        dragStartState = startState;
    }

    void endHandleDrag()
    {
        handleDragItem = nullptr;
        dragItem = nullptr;
        // keep dragStartState until caller has pushed undo
    }

    void beginMove(ImageItem *item, const WorkspaceItemState &startState)
    {
        dragItem = item;
        dragStartState = startState;
    }

    void endMove() { clearMove(); }

    /**
     * Drop any interaction that still holds @p item (item about to be destroyed).
     * Safe when @p item is null or not involved.
     */
    void dropIfItem(const ImageItem *item)
    {
        if (!item) {
            return;
        }
        if (item == dragItem) {
            clearMove();
        }
        if (item == rotateItem) {
            clearRotate();
        }
        if (item == handleDragItem) {
            clearHandleDrag();
        }
    }
};

#endif // ITEMINTERACTSESSION_H
