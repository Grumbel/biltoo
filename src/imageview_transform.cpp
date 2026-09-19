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


void ImageView::pushItemGeometryCommand(const QString &text, ImageItem *item,
                                        const WorkspaceItemState &before,
                                        const WorkspaceItemState &after)
{
    if (!m_undoStack || !item) {
        return;
    }
    class TransformCommand : public QUndoCommand {
    public:
        TransformCommand(ImageView *view, ImageItem *it,
                         const WorkspaceItemState &b, const WorkspaceItemState &a,
                         const QString &label)
            : m_view(view), m_item(it), m_before(b), m_after(a)
        {
            setText(label);
        }
        void undo() override
        {
            if (!m_view || !m_item) {
                return;
            }
            m_view->applyState(m_item, m_before);
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
            m_view->applyState(m_item, m_after);
            if (m_view->isWorkspaceMode()) {
                m_view->updateWorkspaceSceneRect();
            }
            m_view->viewport()->update();
            emit m_view->statusChanged();
        }
    private:
        ImageView *m_view;
        ImageItem *m_item;
        WorkspaceItemState m_before, m_after;
    };
    m_undoStack->push(new TransformCommand(this, item, before, after, text));
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
                m_view->applyLayout(GalleryPackReason::ContentChange);
            }
        }
        ImageView *m_view;
        ImageItem *m_item;
        QImage m_beforeSrc, m_afterSrc;
        WorkspaceItemState m_before, m_after;
    };
    m_undoStack->push(new ContentCommand(this, item, beforeSrc, afterSrc, before, after, text));
}


void ImageView::flipHorizontal()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemFlip(item, true, false);
        if (m_framing.fitMode && isImageMode()) {
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
        if (m_framing.fitMode && isImageMode()) {
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
        if (m_framing.fitMode) {
            fitItem(item, currentFitAspectMode());
        } else if (m_framing.fillMode) {
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


void ImageView::duplicateSelected()
{
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return;
    }
    QList<ImageItem *> sources;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        if (ImageItem *item = targetItem()) {
            sources.append(item);
        }
    }
    if (sources.isEmpty()) {
        return;
    }

    m_scene->clearSelection();
    for (ImageItem *src : sources) {
        // Display-ready copy of current pixels — never createItemFromImage with
        // sourceImage/preview: that ImageCache::put's baked samples as host.
        QImage display = src->sourceImage();
        SessionAppearance::PixelKind kind = SessionAppearance::PixelKind::FullSource;
        if (display.isNull()) {
            display = src->previewImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            display = src->displayImage();
            kind = SessionAppearance::PixelKind::SoftPreview;
        }
        if (display.isNull()) {
            continue;
        }

        WorkspaceItemState content;
        if (src->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_appearance.get(src->sessionId())) {
                content = *app;
            }
        }
        content.path = src->path();
        content.hasCrop = src->sessionHasCrop();
        content.cropRect = src->sessionCropRect();
        content.contentHFlip = src->contentHFlip();
        content.contentVFlip = src->contentVFlip();
        content.colorAdjust = src->colorAdjustments();
        if (src->hasAppliedContentXform()
            && !SessionAppearance::hasContentAppearance(content)) {
            ContentXform::Value x = src->appliedContentXform();
            x.applyToState(content);
        }

        QSize intrinsic = src->imageSize();
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            const QSize native = layoutSizeForPath(src->path(), QImage());
            intrinsic = ContentXform::layoutSize(native, content);
        }
        if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
            // Do not adopt sample pixel size (LQIP/soft).
            intrinsic = QSize(1, 1);
        }

        auto *copy = new ImageItem(src->path(), intrinsic);
        applyItemModeFlags(copy);
        m_scene->addItem(copy);
        m_items.append(copy);
        // Attach already-baked display; do not put into ImageCache.
        attachDisplaySample(copy, display, content, kind);
        m_pendingAppearance.insert(copy, content);
        if (isWorkspaceMode()) {
            copy->setItemScale(src->itemScaleX(), src->itemScaleY());
            copy->setItemShear(src->itemShear());
            copy->setItemRotation(src->itemRotation());
            copy->setItemHFlip(src->itemHFlip());
            copy->setItemVFlip(src->itemVFlip());
            copy->setItemOpacity(src->itemOpacity());
            copy->setStackZ(src->stackZ() + 0.01);
            // Offset so the duplicate is visible beside the original
            copy->setPos(src->pos() + QPointF(40.0, 40.0));
        } else {
            // Gallery: upright tile; MainWindow packs after binding session ids.
            copy->setItemRotation(0.0);
            copy->setItemHFlip(false);
            copy->setItemVFlip(false);
            copy->setItemOpacity(1.0);
            copy->setPos(src->pos());
        }
        copy->setSelected(true);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}

QList<WorkspaceItemState> ImageView::captureSelectedWorkspaceClipboard() const
{
    QList<WorkspaceItemState> out;
    if (!isWorkspaceMode()) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || !m_items.contains(item)) {
            continue;
        }
        WorkspaceItemState s = captureState(item);
        s.path = item->path();
        s.sessionId = item->sessionId();
        // Prefer store for content meta not fully on the item (cropRotation, …).
        if (item->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_appearance.get(item->sessionId())) {
                s.cropRotation = app->cropRotation;
                s.cropSourceSize = app->cropSourceSize;
                s.contentQuarterTurns = app->contentQuarterTurns;
                if (s.colorAdjust.isIdentity() && !app->colorAdjust.isIdentity()) {
                    s.colorAdjust = app->colorAdjust;
                }
            }
        }
        out.append(s);
    }
    return out;
}

void ImageView::removeSelectedCanvasItems()
{
    if (!isWorkspaceMode() || !m_scene) {
        return;
    }
    QList<ImageItem *> toRemove;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (item && m_items.contains(item)) {
            toRemove.append(item);
        }
    }
    if (toRemove.isEmpty()) {
        return;
    }
    setUpdatesEnabled(false);
    m_scene->blockSignals(true);
    for (ImageItem *item : toRemove) {
        // Persist pose + content so filmstrip membership toggle can restore pose.
        rememberItemState(item);
        destroyCanvasItem(item);
    }
    m_scene->blockSignals(false);
    setUpdatesEnabled(true);
    viewport()->update();
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::placeWorkspaceClipboardItems(const QList<WorkspaceItemState> &items,
                                             const QVector<SessionImageId> &newIds,
                                             const QList<int> &sessionIndices)
{
    if (!isWorkspaceMode() || items.isEmpty() || newIds.size() != items.size()) {
        return;
    }
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_bindBook.selectIds.clear();
    for (int i = 0; i < items.size(); ++i) {
        const WorkspaceItemState &st = items.at(i);
        const SessionImageId sid = newIds.at(i);
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        if (st.path.isEmpty() || sid == kInvalidSessionImageId) {
            continue;
        }
        m_bindBook.selectIds.insert(sid);
        // Appearance (content + pose) must already be in the store under sid.
        addImageForSession(st.path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
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

void ImageView::removeCanvasSessionIds(const QList<SessionImageId> &ids)
{
    if (!isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    QList<ImageItem *> toRemove;
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = findItemBySessionId(id)) {
            toRemove.append(item);
        }
    }
    if (toRemove.isEmpty()) {
        return;
    }
    setUpdatesEnabled(false);
    if (m_scene) {
        m_scene->blockSignals(true);
    }
    for (ImageItem *item : toRemove) {
        rememberItemState(item);
        destroyCanvasItem(item);
    }
    if (m_scene) {
        m_scene->blockSignals(false);
    }
    setUpdatesEnabled(true);
    viewport()->update();
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::placeSessionIdsOnCanvas(const QList<SessionImageId> &ids,
                                        const QStringList &paths,
                                        const QList<int> &sessionIndices)
{
    if (!isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_bindBook.selectIds.clear();
    for (int i = 0; i < ids.size(); ++i) {
        const SessionImageId sid = ids.at(i);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        const QString path = (i < paths.size()) ? paths.at(i) : QString();
        if (path.isEmpty()) {
            continue;
        }
        if (findItemBySessionId(sid)) {
            continue; // already on canvas
        }
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        m_bindBook.selectIds.insert(sid);
        addImageForSession(path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}


bool ImageView::targetHasContentAppearance() const
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return false;
    }
    for (const ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentId;
        }
        if (sid != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = m_appearance.get(sid)) {
                if (SessionAppearance::hasContentAppearance(*app)) {
                    return true;
                }
            }
        }
        if (SessionAppearance::liveItemHasContentMods(
                item->sessionHasCrop(), item->contentHFlip(), item->contentVFlip())) {
            return true;
        }
        if (ThumtooCache::hasContentAppearance(item->path())) {
            return true;
        }
    }
    return false;
}

int ImageView::resetContentAppearanceForTargets()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return 0;
    }
    int n = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentId;
        }

        // 1) Drop durable XDG state for this content.
        ThumtooCache::clearContentAppearance(path);

        // 2) Clear session appearance content fields (keep placement).
        if (sid != kInvalidSessionImageId) {
            WorkspaceItemState slot = SessionAppearance::clearedContentOps(
                m_appearance.value(sid));
            slot.sessionId = sid;
            slot.path = path;
            // Keep color grade / pose if present.
            m_appearance.set(sid, slot);
        }
        // Path map still holds content turns from prior bake/pack; captureState
        // re-merges turns==0 from m_itemStateBook.byPath and can resurrect orientation.
        if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
            WorkspaceItemState pathSlot = SessionAppearance::clearedContentOps(*st);
            m_itemStateBook.set(path, pathSlot);
        }

        item->setContentHFlip(false);
        item->setContentVFlip(false);
        item->setSessionCrop(false, QRect());
        item->setItemHFlip(false);
        item->setItemVFlip(false);

        // Restore layout geometry to the unoriented native size (content
        // rotate may have transposed intrinsic).
        {
            QSize native = ThumtooCache::cachedSize(path);
            if (!native.isValid() || native.width() < 1 || native.height() < 1) {
                const QSize known = m_sizeBook.known(path);
                if (!known.isEmpty()) {
                    native = known;
                }
            }
            if (native.isValid() && native.width() > 1 && native.height() > 1
                && native != QSize(1000, 1000) && native != QSize(1024, 1024)) {
                item->setIntrinsicSize(native);
            }
        }

        // 3) Reinstall *mode-appropriate* pixels — never promote a full decode
        // into Gallery soft tiles (that stuck tiles on native res and skipped
        // the soft ladder forever via hasDecodedPixels()).
        //
        //   Gallery  → soft ladder (≤ kGalleryLadderEdge), reset soft state
        //   Image / Workspace → full on-disk decode (user is inspecting / placing)
        //
        // Gallery focused tiles often already hold a *full* oriented decode
        // (Image-mode visit or soft→full upgrade). setPreviewImage no-ops when
        // full source is present, so identity soft would never replace the
        // oriented pixels — Gallery + filmstrip stayed flipped while Image mode
        // (FullSource install) looked correct. Always drop pixels first.
        if (isGalleryMode()) {
            gallerySoftResetPath(path);
            item->clearDecodedPixels();
            const int softEdge = ThumtooCache::kGalleryLadderEdge;
            QImage soft = ImageLoader::loadThumbnail(path, softEdge);
            if (!soft.isNull()) {
                // Identity appearance: SoftPreview install without content bake.
                installDisplayPixels(item, soft, SessionAppearance::PixelKind::SoftPreview,
                                     sid);
            }
            // else: decode window will refill after soft state reset
        } else {
            const QImage full = fullRasterForEdit(path);
            if (!full.isNull()) {
                installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                     sid);
            } else {
                item->clearDecodedPixels();
            }
        }

        if (isImageMode() && m_framing.fitMode) {
            fitItem(item, currentFitAspectMode());
        }

        // Filmstrip: emit current *display* pixels (soft in Gallery, full in Image).
        // Do not emit a separate full decode for Gallery filmstrip overrides.
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, path, appearance);
            }
        }
        ++n;
    }
    if (n > 0 && isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
        // Soft state was reset; kick the ladder for visible tiles.
        updateGalleryDecodeWindow();
    }
    if (n > 0) {
        emit statusChanged();
    }
    return n;
}

