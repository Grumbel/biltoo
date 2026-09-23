// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMINTERACTSESSION_H
#define ITEMINTERACTSESSION_H

#include "imageview_types.h"
#include "item/itemcomponents.h"
#include "item/itemhandle.h"

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
     * Mid-drag scale/shear/rotate read this (paint hot is ImageItem hover).
     */
    ItemHandle handle = ItemHandle::None;

    /** True when beginHandleDrag armed a continuous handle. */
    bool hasContinuousHandle() const { return handle != ItemHandle::None; }
};

/**
 * Single-item Workspace interact scratch (move / free-rotate / handle drag).
 * Phase 7 Stage 2: press-anchor is Placement only (no fat dragStartState DTO).
 */
class ItemInteractSession
{
public:
    void clear()
    {
        clearMove();
        clearRotate();
        clearHandleDrag();
    }

    void clearMove()
    {
        dragItem = nullptr;
        dragStartPlacement = {};
    }

    void clearRotate()
    {
        rotating = false;
        rotateItem = nullptr;
        rotateStartAngle = 0.0;
        dragStartPlacement = {};
    }

    void clearHandleDrag()
    {
        handleDragItem = nullptr;
        handlePress = {};
        dragStartPlacement = {};
    }

    bool isRotating() const { return rotating; }
    bool isHandleDragging() const { return handleDragItem != nullptr; }
    ImageItem *currentRotateItem() const { return rotateItem; }
    ImageItem *currentDragItem() const { return dragItem; }
    ImageItem *currentHandleDragItem() const { return handleDragItem; }

    const ItemComponents::Placement &currentDragStartPlacement() const
    {
        return dragStartPlacement;
    }

    qreal currentRotateStartAngle() const { return rotateStartAngle; }

    void beginRotate(ImageItem *item, qreal startAngle,
                     const ItemComponents::Placement &startPlacement)
    {
        rotating = true;
        rotateItem = item;
        rotateStartAngle = startAngle;
        dragStartPlacement = startPlacement;
    }

    void endRotate() { clearRotate(); }

    void beginHandleDrag(ImageItem *item, const ItemComponents::Placement &startPlacement,
                         const HandlePressScratch &press)
    {
        handleDragItem = item;
        dragStartPlacement = startPlacement;
        handlePress = press;
    }

    HandlePressScratch &handlePressRef() { return handlePress; }
    const HandlePressScratch &handlePressRef() const { return handlePress; }

    void endHandleDrag() { clearHandleDrag(); }

    void beginMove(ImageItem *item, const ItemComponents::Placement &startPlacement)
    {
        dragItem = item;
        dragStartPlacement = startPlacement;
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

private:
    bool rotating = false;
    ImageItem *rotateItem = nullptr;
    /** Pointer angle at press (scene); mid-drag delta uses this with Placement.rotation. */
    qreal rotateStartAngle = 0.0;

    ImageItem *handleDragItem = nullptr;
    HandlePressScratch handlePress;

    ImageItem *dragItem = nullptr;
    ItemComponents::Placement dragStartPlacement;
};

#endif // ITEMINTERACTSESSION_H
