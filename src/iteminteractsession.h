// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMINTERACTSESSION_H
#define ITEMINTERACTSESSION_H

#include "imageview_types.h"
#include "itemcomponents.h"
#include "itemhandle.h"

class ImageItem;

/** Handle-drag press anchors (Stage 2 interact scratch; not durable). */
struct HandlePressScratch {
    QPointF scenePos;
    /** Press-time Workspace pose (scale/shear/rotation); same shape as dragStartPlacement. */
    ItemComponents::Placement placement;
    QPointF itemPos;
    QPointF anchorScene;
    QPointF anchorLocal;
    /**
     * Continuous handle armed at press.
     * Mid-drag scale/shear/rotate read this, not ImageItem::m_activeHandle.
     */
    ItemHandle handle = ItemHandle::None;

    /** True when beginHandleInteraction armed a continuous drag (not a chrome click). */
    bool hasContinuousHandle() const { return handle != ItemHandle::None; }
};

/**
 * Single-item Workspace interaction: move, corner handle scale, free rotate.
 * Distinct from GroupTransformSession (multi-select).
 *
 * Phase 7 Stage 2: pose press-anchor is Placement (dragStartPlacement);
 * full WorkspaceItemState remains for geometry undo (content + pose).
 */
struct ItemInteractSession {
    bool rotating = false;
    ImageItem *rotateItem = nullptr;
    qreal rotateStartAngle = 0.0;
    qreal rotateItemStart = 0.0;

    ImageItem *handleDragItem = nullptr;
    HandlePressScratch handlePress;

    ImageItem *dragItem = nullptr;
    WorkspaceItemState dragStartState;
    ItemComponents::Placement dragStartPlacement;

    void clearRotate()
    {
        rotating = false;
        rotateItem = nullptr;
        rotateStartAngle = 0.0;
        rotateItemStart = 0.0;
    }

    void clearHandleDrag()
    {
        handleDragItem = nullptr;
        handlePress = {};
    }

    void clearMove()
    {
        dragItem = nullptr;
        dragStartState = {};
        dragStartPlacement = {};
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

    ImageItem *currentRotateItem() const { return rotateItem; }

    ImageItem *currentDragItem() const { return dragItem; }

    ImageItem *currentHandleDragItem() const { return handleDragItem; }

    const WorkspaceItemState &currentDragStartState() const { return dragStartState; }

    const ItemComponents::Placement &currentDragStartPlacement() const
    {
        return dragStartPlacement;
    }

    qreal currentRotateStartAngle() const { return rotateStartAngle; }

    qreal currentRotateItemStart() const { return rotateItemStart; }

    void beginRotate(ImageItem *item, qreal startAngle, qreal itemStart,
                     const WorkspaceItemState &startState)
    {
        rotating = true;
        rotateItem = item;
        rotateStartAngle = startAngle;
        rotateItemStart = itemStart;
        dragStartState = startState;
        dragStartPlacement = ItemComponents::placementFromState(startState);
    }

    void endRotate() { clearRotate(); }

    void beginHandleDrag(ImageItem *item, const WorkspaceItemState &startState,
                         const HandlePressScratch &press)
    {
        handleDragItem = item;
        dragStartState = startState;
        dragStartPlacement = ItemComponents::placementFromState(startState);
        handlePress = press;
    }

    HandlePressScratch &handlePressRef() { return handlePress; }
    const HandlePressScratch &handlePressRef() const { return handlePress; }

    void endHandleDrag() { clearHandleDrag(); }

    void beginMove(ImageItem *item, const WorkspaceItemState &startState)
    {
        dragItem = item;
        dragStartState = startState;
        dragStartPlacement = ItemComponents::placementFromState(startState);
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
