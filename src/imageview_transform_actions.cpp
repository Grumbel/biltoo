// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "placementlinear.h"
#include "stackgeometry.h"
#include "thumtoocache.h"
#include "imageloader.h"
#include "sessionappearance.h"
#include "imageitem.h"

#include <QUndoCommand>
#include <QUndoStack>
#include <QtMath>
#include <QGraphicsItem>
#include <algorithm>

namespace {

/** Overlap for stacking: scene AABB of content (rotation expands the box). */
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
        if (StackGeometry::zLess(a->stackZ(), b->stackZ())) {
            return true;
        }
        if (StackGeometry::zGreater(a->stackZ(), b->stackZ())) {
            return false;
        }
        return a < b;
    });
    return layer;
}

} // namespace


void ImageView::flipHorizontal()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemFlip(item, true, false);
        if (m_framing.isFitMode() && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
    emit statusChanged();
}

void ImageView::flipVertical()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemFlip(item, false, true);
        if (m_framing.isFitMode() && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
    emit statusChanged();
}

void ImageView::rotateContentByQuarterTurns(ImageItem *item, int quarterTurns)
{
    // One content-rotate path for Workspace chrome, toolbar, and keyboard.
    // bakeItemRotate90 owns ContentXform + pixels + intrinsic. Placement scale
    // is NOT adjusted: fitting into the pre-rotate AABB (even uniformly) shrinks
    // non-square images on every 90° (min(footW/afterW, footH/afterH) compounds).
    // Workspace scene units = content pixels × scale; intrinsic swap is enough.
    if (!item || quarterTurns == 0) {
        return;
    }

    bakeItemRotate90(item, quarterTurns);

    if (isImageMode()) {
        if (m_framing.isFitMode()) {
            fitItem(item, currentFitAspectMode());
        } else if (m_framing.isFillMode()) {
            fitItem(item, Qt::KeepAspectRatioByExpanding);
        }
    }
}

void ImageView::rotateLeft()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, -1);
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    emit statusChanged();
}

void ImageView::rotateRight()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, 1);
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    emit statusChanged();
}

void ImageView::raiseItem(ImageItem *item)
{
    if (!item || !isWorkspaceMode() || m_items.size() < 2) {
        return;
    }
    // One step: swap z with the next higher overlapping neighbour. Setting
    // z = cover.z+1 skipped intermediates when z values were sparse (e.g. 1→3
    // while 2 was an overlapping neighbour already at 3-epsilon).
    const QList<ImageItem *> layer = overlappingStack(item, m_items);
    const int idx = layer.indexOf(item);
    const int target = StackGeometry::raiseTargetIndex(idx, layer.size());
    if (target < 0) {
        return; // already top among overlapping
    }
    ImageItem *above = layer.at(target);
    const WorkspaceItemState beforeItem = captureState(item);
    const WorkspaceItemState beforeAbove = captureState(above);
    const StackGeometry::ZStep step =
        StackGeometry::raiseStep(item->stackZ(), above->stackZ());
    item->setStackZ(step.selfZ);
    if (step.neighbourChanges) {
        above->setStackZ(step.neighbourZ);
    }
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Raise"));
        pushItemGeometryCommand(tr("Raise"), item, beforeItem, captureState(item));
        if (step.neighbourChanges) {
            pushItemGeometryCommand(tr("Raise"), above, beforeAbove, captureState(above));
        }
        m_undoStack->endMacro();
    }
    emit statusChanged();
}

void ImageView::lowerItem(ImageItem *item)
{
    if (!item || !isWorkspaceMode() || m_items.size() < 2) {
        return;
    }
    const QList<ImageItem *> layer = overlappingStack(item, m_items);
    const int idx = layer.indexOf(item);
    const int target = StackGeometry::lowerTargetIndex(idx, layer.size());
    if (target < 0) {
        return; // already bottom among overlapping
    }
    ImageItem *below = layer.at(target);
    const WorkspaceItemState beforeItem = captureState(item);
    const WorkspaceItemState beforeBelow = captureState(below);
    const StackGeometry::ZStep step =
        StackGeometry::lowerStep(item->stackZ(), below->stackZ());
    item->setStackZ(step.selfZ);
    if (step.neighbourChanges) {
        below->setStackZ(step.neighbourZ);
    }
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Lower"));
        pushItemGeometryCommand(tr("Lower"), item, beforeItem, captureState(item));
        if (step.neighbourChanges) {
            pushItemGeometryCommand(tr("Lower"), below, beforeBelow, captureState(below));
        }
        m_undoStack->endMacro();
    }
    emit statusChanged();
}

void ImageView::raiseSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    // Raise each selection from top-most down so mutual overlaps stay stable.
    QList<ImageItem *> sel;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            sel.append(ii);
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            raiseItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) {
                  return StackGeometry::zGreater(a->stackZ(), b->stackZ())
                      || (qFuzzyCompare(a->stackZ(), b->stackZ()) && a > b);
              });
    for (ImageItem *item : sel) {
        raiseItem(item);
    }
}

void ImageView::lowerSelected()
{
    if (!isWorkspaceMode()) {
        return;
    }
    QList<ImageItem *> sel;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            sel.append(ii);
        }
    }
    if (sel.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            lowerItem(item);
        }
        return;
    }
    std::sort(sel.begin(), sel.end(),
              [](ImageItem *a, ImageItem *b) {
                  return StackGeometry::zLess(a->stackZ(), b->stackZ())
                      || (qFuzzyCompare(a->stackZ(), b->stackZ()) && a < b);
              });
    for (ImageItem *item : sel) {
        lowerItem(item);
    }
}

void ImageView::opacityUp()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        const WorkspaceItemState before = captureState(item);
        item->setItemOpacity(PlacementLinear::opacityAfterStep(item->itemOpacity(), 0.1));
        pushItemGeometryCommand(tr("Opacity"), item, before, captureState(item));
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        const WorkspaceItemState before = captureState(item);
        item->setItemOpacity(PlacementLinear::opacityAfterStep(item->itemOpacity(), -0.1));
        pushItemGeometryCommand(tr("Opacity"), item, before, captureState(item));
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        const WorkspaceItemState before = captureState(item);
        item->setItemOpacity(1.0);
        pushItemGeometryCommand(tr("Reset opacity"), item, before, captureState(item));
        emit statusChanged();
    }
}

void ImageView::resetItemScale()
{
    QList<ImageItem *> targets;
    if (isWorkspaceMode()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        if (ImageItem *tgt = targetItem()) {
            targets.append(tgt);
        } else if (ImageItem *p = primaryItem()) {
            targets.append(p);
        }
    }
    if (targets.isEmpty()) {
        return;
    }
    const bool macro = m_undoStack && targets.size() > 1;
    if (macro) {
        m_undoStack->beginMacro(tr("Reset scale"));
    }
    for (ImageItem *item : targets) {
        const WorkspaceItemState before = captureState(item);
        item->setItemScale(1.0, 1.0);
        item->setItemShear(0.0);
        pushItemGeometryCommand(tr("Reset scale"), item, before, captureState(item));
    }
    if (macro) {
        m_undoStack->endMacro();
    }
    emit statusChanged();
    viewport()->update();
}

void ImageView::resetItemRotation()
{
    QList<ImageItem *> targets;
    if (isWorkspaceMode()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        if (ImageItem *tgt = targetItem()) {
            targets.append(tgt);
        } else if (ImageItem *p = primaryItem()) {
            targets.append(p);
        }
    }
    if (targets.isEmpty()) {
        return;
    }
    const bool macro = m_undoStack && targets.size() > 1;
    if (macro) {
        m_undoStack->beginMacro(tr("Reset rotation"));
    }
    for (ImageItem *item : targets) {
        const WorkspaceItemState before = captureState(item);
        item->setItemRotation(0.0);
        commitItemSessionEdit(item);
        pushItemGeometryCommand(tr("Reset rotation"), item, before, captureState(item));
    }
    if (macro) {
        m_undoStack->endMacro();
    }
    viewport()->update();
}

void ImageView::resetItemShear()
{
    QList<ImageItem *> targets;
    if (isWorkspaceMode()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        if (ImageItem *tgt = targetItem()) {
            targets.append(tgt);
        } else if (ImageItem *p = primaryItem()) {
            targets.append(p);
        }
    }
    if (targets.isEmpty()) {
        return;
    }
    const bool macro = m_undoStack && targets.size() > 1;
    if (macro) {
        m_undoStack->beginMacro(tr("Reset shear"));
    }
    for (ImageItem *item : targets) {
        const WorkspaceItemState before = captureState(item);
        item->setItemShear(0.0);
        pushItemGeometryCommand(tr("Reset shear"), item, before, captureState(item));
    }
    if (macro) {
        m_undoStack->endMacro();
    }
    emit statusChanged();
    viewport()->update();
}


qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    if (!item) {
        return 0.0;
    }
    return PlacementLinear::angleAbout(item->scenePos(), scenePos);
}

qreal ImageView::cardinalRotationOrZero(qreal degrees)
{
    // Image mode: always nearest 90° content orientation. Free Workspace tilt
    // (residual off the cardinal) is discarded — never shown as an arbitrary angle.
    return PlacementLinear::cardinalRotationOrZero(degrees);
}


