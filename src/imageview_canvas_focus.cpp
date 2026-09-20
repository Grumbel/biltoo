// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas focus, primary/target selection, placeholders, and item destroy.

#include "imageview.h"
#include "imageitem.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "viewtransform.h"

#include <QSet>
#include <QUndoStack>
#include <QGraphicsItem>

ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    // Transform targets:
    //   Image → primary (sole) canvas object
    //   Gallery / Workspace → first selected item; Workspace also falls back to
    //   the sole object when the selection is empty
    if (!m_scene) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            // Selection can briefly hold stale pointers after destroyCanvasItem.
            if (!m_items.contains(item) || item->scene() != m_scene) {
                continue;
            }
            return item;
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    return nullptr;
}

void ImageView::ensureGalleryPlaceholders()
{
    m_gallery.ensurePlaceholders();
}


// --- from imageview_layout.cpp (canvas) ---










int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            ++n;
        }
    }
    return n;
}


void ImageView::removeWorkspacePathOccurrence(const QString &path, int occurrence)
{
    if (occurrence < 0) {
        return;
    }
    int found = 0;
    for (ImageItem *item : m_items) {
        if (item->path() != path) {
            continue;
        }
        if (found == occurrence) {
            takePendingWorkspacePath(path);
            m_displayPipeline.loadGate().removePendingScenePos(path);
            m_bindBook.removeIndexForPath(path);
        m_displayPipeline.gallerySoftResetPath(path);
destroyCanvasItem(item);
            emit statusChanged();
            emit workspacePathsChanged();
            return;
        }
        ++found;
    }
}


void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (isGalleryMode()) {
        // Open/TTFP: first index is often already in view after pack — ensureVisible
        // on a large scene was hundreds of ms for no visual change.
        bool needScroll = true;
        if (viewport()) {
            const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
            if (vis.isValid() && item->sceneBoundingRect().intersects(vis)) {
                needScroll = false;
            }
        }
        if (needScroll) {
            ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
        }
        // Keyboard focus: show filename in the HUD like mouse hover.
        if (m_gallery.hoverPath() != path) {
            m_gallery.setHoverPath(path);
            viewport()->update();
        }
    }
}


void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}


void ImageView::destroyCanvasItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Stage 2: drop tile session and release pipeline-owned bag before delete.
    m_displayPipeline.dropItemTileLodSession(item);
    m_displayPipeline.releaseTileBag(item);
    unregisterItemDisplaySurface(item);
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_items.contains(item);
    const bool inGalleryStash = m_gallery.stashedItems().contains(item);
    const bool inWorkspaceStash = m_workspace.stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    m_itemInteract.dropIfItem(item);
    if (item == m_gallery.selectionAnchor()) {
        m_gallery.setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (m_groupXform.isScaleDrag() || m_groupXform.isRotateDrag() || m_groupXform.hasDragItems()) {
        m_groupXform.endDrag();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_gallery.stashedItems().removeAll(item);
    m_workspace.stashedItems().removeAll(item);

    rememberItemState(item);
    m_items.removeAll(item);
    // Off-canvas neighbor prefetch may still hold a controller for this path.
    if (!path.isEmpty() && !pathOnLiveCanvas(path)) {
        dropTilePrefetchPath(path);
    }
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (m_undoStack && !m_preserveUndoOnDestroy) {
        m_undoStack->clear();
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}






// --- session remove / bind residual from layout ---












// --- canvas clipboard / session place-remove (from transform) ---






