// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Geometry and content undo commands + ImageView push helpers.
// Bodies live here so imageview_transform_actions.cpp stays thin routers only.

#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "gallery/gallerycontroller.h"

#include <QUndoCommand>
#include <QUndoStack>

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
