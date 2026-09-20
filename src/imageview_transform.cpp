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
