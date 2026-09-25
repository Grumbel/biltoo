// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Geometry and content undo commands + ImageView push helpers.
// Bodies live here so imageview_transform_actions.cpp stays thin routers only.

#include "imageview.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "item/itemcomponents.h"
#include "display/displaypipelinecontroller.h"
#include "crop/cropcontroller.h"
#include "gallery/gallerycontroller.h"
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

void ImageView::pushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                                      const ItemComponents::Placement &after, const QString &text)
{
    if (!item) {
        return;
    }
    if (ItemComponents::placementNearlyEqual(before, after)) {
        return;
    }
    // Single geometry undo path (persist + Placement command).
    pushItemGeometryCommand(text, item, before, after);
    emit statusChanged();
}

void ImageView::hostPushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                                          const ItemComponents::Placement &after, const QString &text)
{
    pushItemTransformUndo(item, before, after, text);
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
            m_view->hostCrop().applyCropAppearance(m_item, src, st);
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


void ImageView::pushSessionContentCommand(const QString &text, SessionImageId sid,
                                          const QString &path,
                                          const WorkspaceItemState &before,
                                          const WorkspaceItemState &after)
{
    if (!m_undoStack || sid == kInvalidSessionImageId) {
        return;
    }
    class SessionContentCommand : public QUndoCommand {
    public:
        SessionContentCommand(ImageView *view, SessionImageId id, const QString &p,
                              const WorkspaceItemState &b, const WorkspaceItemState &a,
                              const QString &label)
            : m_view(view), m_sid(id), m_path(p), m_before(b), m_after(a)
        {
            setText(label);
        }
        void undo() override { apply(m_before); }
        void redo() override { apply(m_after); }
    private:
        void apply(const WorkspaceItemState &st)
        {
            if (!m_view || m_sid == kInvalidSessionImageId) {
                return;
            }
            if (ImageItem *item = m_view->findItemBySessionId(m_sid)) {
                m_view->hostCrop().applyCropAppearance(item, QImage(), st);
                m_view->hostDisplayPipeline().rematerializeItemContent(item, st);
                if (m_view->isGalleryMode()) {
                    m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
                }
                return;
            }
            // Durable only — virtual Gallery slot / filmstrip row.
            WorkspaceItemState slot = st;
            slot.sessionId = m_sid;
            if (!m_path.isEmpty()) {
                slot.path = m_path;
            }
            m_view->itemWorld().setCrop(m_sid, ItemComponents::cropFromState(slot));
            m_view->itemWorld().setContentBake(m_sid, ItemComponents::contentBakeFromState(slot));
            if (!slot.colorAdjust.isIdentity()) {
                ItemComponents::Color c;
                c.grade = slot.colorAdjust;
                m_view->itemWorld().setColor(m_sid, c);
            }
            const QString path = slot.path.isEmpty() ? m_path : slot.path;
            QImage appearance;
            if (!path.isEmpty()) {
                appearance = ImageCache::get(path);
                if (!appearance.isNull()) {
                    appearance = SessionAppearance::applyContentToImage(
                        appearance, slot, SessionAppearance::PixelKind::SoftPreview);
                }
            }
            if (!appearance.isNull()) {
                emit m_view->sessionAppearanceChanged(m_sid, path, appearance);
            }
        }
        ImageView *m_view = nullptr;
        SessionImageId m_sid = kInvalidSessionImageId;
        QString m_path;
        WorkspaceItemState m_before;
        WorkspaceItemState m_after;
    };
    m_undoStack->push(new SessionContentCommand(this, sid, path, before, after, text));
}
