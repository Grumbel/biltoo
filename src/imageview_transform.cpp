// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
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
    // Prefer AABB so partial / edge overlaps still count as a stack step.
    // Polygon-only tests were missing some overlaps and made Raise jump.
    if (a->contentSceneRect().intersects(b->contentSceneRect())) {
        return true;
    }
    const QPolygonF pa = a->contentScenePolygon();
    const QPolygonF pb = b->contentScenePolygon();
    if (pa.isEmpty() || pb.isEmpty()) {
        return false;
    }
    if (pa.intersects(pb)) {
        return true;
    }
    if (pb.containsPoint(pa.boundingRect().center(), Qt::OddEvenFill)
        || pa.containsPoint(pb.boundingRect().center(), Qt::OddEvenFill)) {
        return true;
    }
    return false;
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
        if (!qFuzzyCompare(a->stackZ(), b->stackZ())) {
            return a->stackZ() < b->stackZ();
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
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
}

void ImageView::flipVertical()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemFlip(item, false, true);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
}

void ImageView::rotateLeft()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemRotate90(item, -1);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
}

void ImageView::rotateRight()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        bakeItemRotate90(item, 1);
        if (m_fitMode && isImageMode()) {
            fitItem(item, currentFitAspectMode());
        }
    }
    if (isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
    }
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
    if (idx < 0 || idx + 1 >= layer.size()) {
        return; // already top among overlapping
    }
    ImageItem *above = layer.at(idx + 1);
    const WorkspaceItemState beforeItem = captureState(item);
    const WorkspaceItemState beforeAbove = captureState(above);
    const qreal za = item->stackZ();
    const qreal zb = above->stackZ();
    if (qFuzzyCompare(za, zb)) {
        item->setStackZ(zb + 1.0);
    } else {
        item->setStackZ(zb);
        above->setStackZ(za);
    }
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Raise"));
        pushItemGeometryCommand(tr("Raise"), item, beforeItem, captureState(item));
        pushItemGeometryCommand(tr("Raise"), above, beforeAbove, captureState(above));
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
    if (idx <= 0) {
        return; // already bottom among overlapping
    }
    ImageItem *below = layer.at(idx - 1);
    const WorkspaceItemState beforeItem = captureState(item);
    const WorkspaceItemState beforeBelow = captureState(below);
    const qreal za = item->stackZ();
    const qreal zb = below->stackZ();
    if (qFuzzyCompare(za, zb)) {
        item->setStackZ(zb - 1.0);
    } else {
        item->setStackZ(zb);
        below->setStackZ(za);
    }
    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Lower"));
        pushItemGeometryCommand(tr("Lower"), item, beforeItem, captureState(item));
        pushItemGeometryCommand(tr("Lower"), below, beforeBelow, captureState(below));
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
              [](ImageItem *a, ImageItem *b) { return a->stackZ() > b->stackZ(); });
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
              [](ImageItem *a, ImageItem *b) { return a->stackZ() < b->stackZ(); });
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
        item->setItemOpacity(item->itemOpacity() + 0.1);
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
        item->setItemOpacity(item->itemOpacity() - 0.1);
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
        // Copy current displayed pixels as-is (no second session-crop pass).
        // Value copy of current pixels + appearance (not a shared reference).
        ImageItem *copy = createItemFromImage(src->path(), src->sourceImage(),
                                              /*applyStoredSessionCrop=*/false);
        if (!copy) {
            continue;
        }
        copy->setContentHFlip(src->contentHFlip());
        copy->setContentVFlip(src->contentVFlip());
        copy->setSessionCrop(src->sessionHasCrop(), src->sessionCropRect());
        // Non-destructive colour grade must be on the live item immediately so
        // the duplicate tile matches the source before session-id bind.
        copy->setColorAdjustments(src->colorAdjustments());
        // Stage full content appearance (incl. cropRotation + colorAdjust) for the new session id.
        {
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
            m_pendingItemAppearance.insert(copy, content);
        }
        if (isWorkspaceMode()) {
            copy->setItemScale(src->itemScaleX(), src->itemScaleY());
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
    m_pendingSelectSessionIds.clear();
    for (int i = 0; i < items.size(); ++i) {
        const WorkspaceItemState &st = items.at(i);
        const SessionImageId sid = newIds.at(i);
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        if (st.path.isEmpty() || sid == kInvalidSessionImageId) {
            continue;
        }
        m_pendingSelectSessionIds.insert(sid);
        // Appearance (content + pose) must already be in the store under sid.
        addImageForSession(st.path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}

qreal ImageView::angleAt(const QPointF &scenePos, ImageItem *item) const
{
    const QPointF c = item->scenePos();
    return qRadiansToDegrees(std::atan2(scenePos.y() - c.y(), scenePos.x() - c.x()));
}

qreal ImageView::cardinalRotationOrZero(qreal degrees)
{
    // Image mode: always nearest 90° content orientation. Free Workspace tilt
    // (residual off the cardinal) is discarded — never shown as an arbitrary angle.
    const qreal n = std::fmod(std::fmod(degrees, 360.0) + 360.0, 360.0);
    qreal snapped = qRound(n / 90.0) * 90.0;
    if (snapped >= 360.0) {
        snapped = 0.0;
    }
    return snapped;
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
    m_pendingSelectSessionIds.clear();
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
        m_pendingSelectSessionIds.insert(sid);
        addImageForSession(path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}
