// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace stack z-order (raise/lower by scene overlap) and opacity edits.
// ImageView keeps thin public routers for MainWindow actions.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "workspace/stackgeometry.h"
#include "item/placementlinear.h"
#include "item/itemcomponents.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QUndoStack>
#include <algorithm>

namespace {

bool contentOverlaps(const ImageItem *a, const ImageItem *b)
{
    if (!a || !b || a == b) {
        return false;
    }
    return StackGeometry::contentOverlaps(
        a->contentSceneRect(), a->contentScenePolygon(),
        b->contentSceneRect(), b->contentScenePolygon());
}

/** Overlapping stack including @p item, sorted bottom → top (stable on ties). */
QList<ImageItem *> overlappingStack(ImageItem *item, const QList<ImageItem *> &all)
{
    QList<ImageItem *> layer;
    if (!item) {
        return layer;
    }
    layer.append(item);
    for (ImageItem *other : all) {
        if (other && other != item && contentOverlaps(item, other)) {
            layer.append(other);
        }
    }
    std::sort(layer.begin(), layer.end(), [](ImageItem *a, ImageItem *b) {
        const qreal za = a->placement().z;
        const qreal zb = b->placement().z;
        if (StackGeometry::zLess(za, zb)) {
            return true;
        }
        if (StackGeometry::zGreater(za, zb)) {
            return false;
        }
        return a < b;
    });
    return layer;
}

} // namespace

void WorkspaceController::raiseItem(ImageItem *item)
{
    if (!item || !m_view->isWorkspaceMode() || m_view->liveItems().size() < 2) {
        return;
    }
    // One step: swap z with the next higher overlapping neighbour. Setting
    // z = cover.z+1 skipped intermediates when z values were sparse (e.g. 1→3
    // while 2 was an overlapping neighbour already at 3-epsilon).
    const QList<ImageItem *> layer = overlappingStack(item, m_view->liveItems());
    const int idx = layer.indexOf(item);
    const int target = StackGeometry::raiseTargetIndex(idx, layer.size());
    if (target < 0) {
        return; // already top among overlapping
    }
    ImageItem *above = layer.at(target);
    const ItemComponents::Placement beforeItem = item->placement();
    const ItemComponents::Placement beforeAbove = above->placement();
    const StackGeometry::ZStep step =
        StackGeometry::raiseStep(item->placement().z, above->placement().z);
    {
        ItemComponents::Placement pl = item->placement();
        pl.z = step.selfZ;
        item->applyPlacement(pl);
    }
    if (step.neighbourChanges) {
        ItemComponents::Placement pl = above->placement();
        pl.z = step.neighbourZ;
        above->applyPlacement(pl);
    }
    QUndoStack *stack = m_view->hostUndoStack();
    if (stack) {
        stack->beginMacro(m_view->tr("Raise"));
    }
    m_view->hostPushItemTransformUndo(item, beforeItem, item->placement(),
                                      m_view->tr("Raise"));
    if (step.neighbourChanges) {
        m_view->hostPushItemTransformUndo(above, beforeAbove, above->placement(),
                                          m_view->tr("Raise"));
    }
    if (stack) {
        stack->endMacro();
    }
}

void WorkspaceController::lowerItem(ImageItem *item)
{
    if (!item || !m_view->isWorkspaceMode() || m_view->liveItems().size() < 2) {
        return;
    }
    const QList<ImageItem *> layer = overlappingStack(item, m_view->liveItems());
    const int idx = layer.indexOf(item);
    const int target = StackGeometry::lowerTargetIndex(idx, layer.size());
    if (target < 0) {
        return; // already bottom among overlapping
    }
    ImageItem *below = layer.at(target);
    const ItemComponents::Placement beforeItem = item->placement();
    const ItemComponents::Placement beforeBelow = below->placement();
    const StackGeometry::ZStep step =
        StackGeometry::lowerStep(item->placement().z, below->placement().z);
    {
        ItemComponents::Placement pl = item->placement();
        pl.z = step.selfZ;
        item->applyPlacement(pl);
    }
    if (step.neighbourChanges) {
        ItemComponents::Placement pl = below->placement();
        pl.z = step.neighbourZ;
        below->applyPlacement(pl);
    }
    QUndoStack *stack = m_view->hostUndoStack();
    if (stack) {
        stack->beginMacro(m_view->tr("Lower"));
    }
    m_view->hostPushItemTransformUndo(item, beforeItem, item->placement(),
                                      m_view->tr("Lower"));
    if (step.neighbourChanges) {
        m_view->hostPushItemTransformUndo(below, beforeBelow, below->placement(),
                                          m_view->tr("Lower"));
    }
    if (stack) {
        stack->endMacro();
    }
}

void WorkspaceController::raiseSelected()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    // Raise each selection from top-most down so mutual overlaps stay stable.
    QList<ImageItem *> sel;
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        for (QGraphicsItem *gi : scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                sel.append(ii);
            }
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = m_view->targetItem()) {
            raiseItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) {
                  const qreal za = a->placement().z;
                  const qreal zb = b->placement().z;
                  return StackGeometry::zGreater(za, zb)
                      || (qFuzzyCompare(za, zb) && a > b);
              });
    for (ImageItem *item : sel) {
        raiseItem(item);
    }
}

void WorkspaceController::lowerSelected()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    QList<ImageItem *> sel;
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        for (QGraphicsItem *gi : scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                sel.append(ii);
            }
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = m_view->targetItem()) {
            lowerItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) {
                  const qreal za = a->placement().z;
                  const qreal zb = b->placement().z;
                  return StackGeometry::zLess(za, zb)
                      || (qFuzzyCompare(za, zb) && a < b);
              });
    for (ImageItem *item : sel) {
        lowerItem(item);
    }
}

void WorkspaceController::opacityUp()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = m_view->targetItem()) {
        const ItemComponents::Placement before = item->placement();
        ItemComponents::Placement pl = item->placement();
        pl.opacity = PlacementLinear::opacityAfterStep(pl.opacity, 0.1);
        item->applyPlacement(pl);
        m_view->hostPushItemTransformUndo(item, before, item->placement(),
                                          m_view->tr("Opacity"));
    }
}

void WorkspaceController::opacityDown()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = m_view->targetItem()) {
        const ItemComponents::Placement before = item->placement();
        ItemComponents::Placement pl = item->placement();
        pl.opacity = PlacementLinear::opacityAfterStep(pl.opacity, -0.1);
        item->applyPlacement(pl);
        m_view->hostPushItemTransformUndo(item, before, item->placement(),
                                          m_view->tr("Opacity"));
    }
}

void WorkspaceController::opacityReset()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = m_view->targetItem()) {
        const ItemComponents::Placement before = item->placement();
        ItemComponents::Placement pl = item->placement();
        pl.opacity = 1.0;
        item->applyPlacement(pl);
        m_view->hostPushItemTransformUndo(item, before, item->placement(),
                                          m_view->tr("Reset opacity"));
    }
}
