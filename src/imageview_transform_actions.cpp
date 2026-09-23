// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "item/itemcomponents.h"
#include "item/placementlinear.h"
#include "workspace/stackgeometry.h"
#include "host/thumtoocache.h"
#include "imageloader.h"
#include "session/sessionappearance.h"
#include "imageitem.h"

#include <QUndoCommand>
#include <QUndoStack>
#include <QtMath>
#include <QGraphicsItem>
#include <algorithm>

/** Geometry undo/redo — friend of ImageView; stores Placement only (Stage 2). */
class ImageViewTransformGeometryCommand : public QUndoCommand {
public:
    ImageViewTransformGeometryCommand(ImageView *view, ImageItem *item,
                                      const ItemComponents::Placement &before,
                                      const ItemComponents::Placement &after,
                                      const QString &label)
        : m_view(view)
        , m_item(item)
        , m_before(before)
        , m_after(after)
    {
        setText(label);
    }

    void undo() override
    {
        if (!m_view || !m_item) {
            return;
        }
        m_view->applyGeometrySessionState(m_item, m_before);
        if (m_view->isWorkspaceMode()) {
            m_view->updateWorkspaceSceneRect();
        }
        m_view->viewport()->update();
        emit m_view->statusChanged();
    }

    void redo() override
    {
        if (!m_view || !m_item) {
            return;
        }
        m_view->applyGeometrySessionState(m_item, m_after);
        if (m_view->isWorkspaceMode()) {
            m_view->updateWorkspaceSceneRect();
        }
        m_view->viewport()->update();
        emit m_view->statusChanged();
    }

private:
    ImageView *m_view = nullptr;
    ImageItem *m_item = nullptr;
    ItemComponents::Placement m_before;
    ItemComponents::Placement m_after;
};

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
        m_gallery.applyLayout(GalleryPackReason::ContentChange);
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
        m_gallery.applyLayout(GalleryPackReason::ContentChange);
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
        m_gallery.applyLayout(GalleryPackReason::ContentChange);
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
        m_gallery.applyLayout(GalleryPackReason::ContentChange);
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
    const ItemComponents::Placement beforeItem = placementFromItem(item);
    const ItemComponents::Placement beforeAbove = placementFromItem(above);
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
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Raise"));
        pushItemGeometryCommand(tr("Raise"), item, beforeItem, placementFromItem(item));
        if (step.neighbourChanges) {
            pushItemGeometryCommand(tr("Raise"), above, beforeAbove, placementFromItem(above));
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
    const ItemComponents::Placement beforeItem = placementFromItem(item);
    const ItemComponents::Placement beforeBelow = placementFromItem(below);
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
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Lower"));
        pushItemGeometryCommand(tr("Lower"), item, beforeItem, placementFromItem(item));
        if (step.neighbourChanges) {
            pushItemGeometryCommand(tr("Lower"), below, beforeBelow, placementFromItem(below));
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
                  const qreal za = a->placement().z;
                  const qreal zb = b->placement().z;
                  return StackGeometry::zGreater(za, zb)
                      || (qFuzzyCompare(za, zb) && a > b);
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
                  const qreal za = a->placement().z;
                  const qreal zb = b->placement().z;
                  return StackGeometry::zLess(za, zb)
                      || (qFuzzyCompare(za, zb) && a < b);
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
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.opacity = PlacementLinear::opacityAfterStep(pl.opacity, 0.1);
        item->applyPlacement(pl);
        pushItemGeometryCommand(tr("Opacity"), item, before, placementFromItem(item));
        emit statusChanged();
    }
}

void ImageView::opacityDown()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.opacity = PlacementLinear::opacityAfterStep(pl.opacity, -0.1);
        item->applyPlacement(pl);
        pushItemGeometryCommand(tr("Opacity"), item, before, placementFromItem(item));
        emit statusChanged();
    }
}

void ImageView::opacityReset()
{
    if (!isWorkspaceMode()) {
        return;
    }
    if (ImageItem *item = targetItem()) {
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.opacity = 1.0;
        item->applyPlacement(pl);
        pushItemGeometryCommand(tr("Reset opacity"), item, before, placementFromItem(item));
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
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.scale = 1.0;
        pl.scaleY = 1.0;
        pl.shear = 0.0;
        item->applyPlacement(pl);
        pushItemGeometryCommand(tr("Reset scale"), item, before, placementFromItem(item));
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
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.rotation = 0.0;
        item->applyPlacement(pl);
        commitItemSessionEdit(item);
        pushItemGeometryCommand(tr("Reset rotation"), item, before, placementFromItem(item));
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
        const ItemComponents::Placement before = placementFromItem(item);
        ItemComponents::Placement pl = item->placement();
        pl.shear = 0.0;
        item->applyPlacement(pl);
        pushItemGeometryCommand(tr("Reset shear"), item, before, placementFromItem(item));
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

void ImageView::pushItemGeometryCommand(const QString &text, ImageItem *item,
                                        const ItemComponents::Placement &before,
                                        const ItemComponents::Placement &after)
{
    if (!item) {
        return;
    }
    // Forward path: item already has `after` pose; keep ItemWorld Placement in sync.
    persistGeometrySessionState(item, after);
    if (!m_undoStack) {
        return;
    }
    m_undoStack->push(
        new ImageViewTransformGeometryCommand(this, item, before, after, text));
}

void ImageView::pushItemContentCommand(const QString &text, ImageItem *item,
                                       const QImage &beforeSrc, const QImage &afterSrc,
                                       const WorkspaceItemState &before,
                                       const WorkspaceItemState &after)
{
    if (!m_undoStack || !item) {
        return;
    }
    class ContentCommand : public QUndoCommand {
    public:
        ContentCommand(ImageView *view, ImageItem *it,
                       const QImage &bSrc, const QImage &aSrc,
                       const WorkspaceItemState &b, const WorkspaceItemState &a,
                       const QString &label)
            : m_view(view), m_item(it)
            , m_beforeSrc(bSrc), m_afterSrc(aSrc)
            , m_before(b), m_after(a)
        {
            setText(label);
        }
        void undo() override { apply(m_beforeSrc, m_before); }
        void redo() override { apply(m_afterSrc, m_after); }
    private:
        void apply(const QImage &src, const WorkspaceItemState &st)
        {
            if (!m_view || !m_item) {
                return;
            }
            // Reuse crop appearance path: pixels + content flags + geometry + session store.
            m_view->applyCropAppearance(m_item, src, st);
            if (m_view->isGalleryMode()) {
                m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
            }
        }
        ImageView *m_view;
        ImageItem *m_item;
        QImage m_beforeSrc, m_afterSrc;
        WorkspaceItemState m_before, m_after;
    };
    m_undoStack->push(new ContentCommand(this, item, beforeSrc, afterSrc, before, after, text));
}
