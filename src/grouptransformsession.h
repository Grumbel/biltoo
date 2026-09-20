// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GROUPTRANSFORMSESSION_H
#define GROUPTRANSFORMSESSION_H

#include "imageview_types.h"
#include "itemcomponents.h"

#include <QList>
#include <QPointF>
#include <QRectF>

class ImageItem;

/**
 * Multi-select group scale/rotate drag gesture (Workspace).
 * Handle indices: 0–7 scale, 8–11 rotate; -1 = none.
 *
 * Phase 7 Stage 2: dragStartPlacements mirrors pose; full states stay for undo.
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
        dragStartPlacements.clear();
        dragItems.clear();
    }

    bool active() const { return scaleDrag || rotateDrag; }

    bool isScaleDrag() const { return scaleDrag; }

    bool isRotateDrag() const { return rotateDrag; }

    /** Paint hot: hover only (beginDrag sticks hover; view freezes it while active). */
    bool isHandleHot(int id) const
    {
        return hoverHandle == id;
    }

    bool hasHoverHandle() const { return hoverHandle != -1; }

    /** Active continuous handle index during scale/rotate (-1 if idle). */
    int currentHandle() const { return handle; }

    const QPointF &centerStartPoint() const { return centerStart; }

    const QRectF &boundsStartRect() const { return boundsStart; }

    const QPointF &pressScenePosPoint() const { return pressScenePos; }

    qreal pressAngle() const { return pressAngleDeg; }

    /** @return true when hover handle index changed. */
    bool setHoverHandle(int h)
    {
        if (hoverHandle == h) {
            return false;
        }
        hoverHandle = h;
        return true;
    }

    void clearHover() { hoverHandle = -1; }

    void setPressScenePos(const QPointF &p) { pressScenePos = p; }

    void setPressAngleDeg(qreal deg) { pressAngleDeg = deg; }

    /** Start scale or rotate drag for the given handle and selection snapshot. */
    void beginDrag(int h, bool isRotate, const QRectF &bounds,
                   const QList<ImageItem *> &items,
                   const QList<ItemComponents::Placement> &placements)
    {
        handle = h;
        // Paint hot sticks for the drag: hover is frozen while scale/rotate active.
        hoverHandle = h;
        scaleDrag = !isRotate;
        rotateDrag = isRotate;
        boundsStart = bounds;
        centerStart = bounds.center();
        dragItems = items;
        dragStartPlacements = placements;
    }

    /** Mark drag slot @p i as null (item left canvas mid-drag). */
    void nullDragItemAt(int i)
    {
        if (i >= 0 && i < dragItems.size()) {
            dragItems[i] = nullptr;
        }
    }

    /** Remove null items from drag lists (item destroyed mid-drag). */
    void pruneNullItems()
    {
        for (int i = dragItems.size() - 1; i >= 0; --i) {
            if (!dragItems.at(i)) {
                dragItems.removeAt(i);
                if (i < dragStartPlacements.size()) {
                    dragStartPlacements.removeAt(i);
                }
            }
        }
    }

    bool dragListsAligned() const
    {
        return !dragItems.isEmpty()
            && dragStartPlacements.size() == dragItems.size();
    }

    int dragCount() const { return dragItems.size(); }

    ImageItem *dragItemAt(int i) const
    {
        return (i >= 0 && i < dragItems.size()) ? dragItems.at(i) : nullptr;
    }

    void setDragItemAt(int i, ImageItem *item)
    {
        if (i >= 0 && i < dragItems.size()) {
            dragItems[i] = item;
        }
    }

    const ItemComponents::Placement &dragStartPlacementAt(int i) const
    {
        return dragStartPlacements.at(i);
    }

    bool hasDragItems() const { return !dragItems.isEmpty(); }

    /** Drop active scale/rotate drag (also clears hover). */
    void endDrag()
    {
        handle = -1;
        hoverHandle = -1;
        scaleDrag = false;
        rotateDrag = false;
        boundsStart = {};
        centerStart = {};
        pressScenePos = {};
        pressAngleDeg = 0.0;
        dragStartPlacements.clear();
        dragItems.clear();
    }

private:
    int handle = -1;
    int hoverHandle = -1;
    bool scaleDrag = false;
    bool rotateDrag = false;
    QRectF boundsStart;
    QPointF centerStart;
    QPointF pressScenePos;
    qreal pressAngleDeg = 0.0;
    QList<ItemComponents::Placement> dragStartPlacements;
    QList<ImageItem *> dragItems;
};

#endif // GROUPTRANSFORMSESSION_H
