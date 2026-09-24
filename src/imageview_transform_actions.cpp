// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "item/itemcomponents.h"
#include "item/placementlinear.h"
#include "host/thumtoocache.h"
#include "host/imageloader.h"
#include "session/sessionappearance.h"
#include "imageitem.h"

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

void ImageView::flipHorizontal()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        m_displayPipeline->bakeItemFlip(item, true, false);
        if (m_image.framing().isFitMode() && isImageMode()) {
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
        m_displayPipeline->bakeItemFlip(item, false, true);
        if (m_image.framing().isFitMode() && isImageMode()) {
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
    // DisplayPipelineController::bakeItemRotate90 composes want + ItemWorld/undo + pixels. Placement scale
    // is NOT adjusted: fitting into the pre-rotate AABB (even uniformly) shrinks
    // non-square images on every 90° (min(footW/afterW, footH/afterH) compounds).
    // Workspace scene units = content pixels × scale; intrinsic swap is enough.
    if (!item || quarterTurns == 0) {
        return;
    }

    m_displayPipeline->bakeItemRotate90(item, quarterTurns);

    if (isImageMode()) {
        if (m_image.framing().isFitMode()) {
            fitItem(item, currentFitAspectMode());
        } else if (m_image.framing().isFillMode()) {
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
    m_workspace.raiseItem(item);
}

void ImageView::lowerItem(ImageItem *item)
{
    m_workspace.lowerItem(item);
}

void ImageView::raiseSelected()
{
    m_workspace.raiseSelected();
}

void ImageView::lowerSelected()
{
    m_workspace.lowerSelected();
}

void ImageView::opacityUp()
{
    m_workspace.opacityUp();
}

void ImageView::opacityDown()
{
    m_workspace.opacityDown();
}

void ImageView::opacityReset()
{
    m_workspace.opacityReset();
}

void ImageView::resetItemScale()
{
    m_workspace.resetItemScale();
}

void ImageView::resetItemRotation()
{
    m_workspace.resetItemRotation();
}

void ImageView::resetItemShear()
{
    m_workspace.resetItemShear();
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
