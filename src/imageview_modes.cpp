// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QScrollBar>
#include <QUndoStack>
#include <QTimer>

void ImageView::clearInteractionState()
{
    m_handleDragItem = nullptr;
    m_groupScaleDrag = false;
    m_groupRotateDrag = false;
    m_groupHandle = -1;
    m_groupHoverHandle = -1;
    m_groupDragItems.clear();
    m_groupDragStartStates.clear();
    m_rotateItem = nullptr;
    m_rotating = false;
    m_dragItem = nullptr;
    m_gallery.setSelectionAnchor(nullptr);
}

void ImageView::clearLiveCanvas()
{
    // Destroy only the live scene items. Mode stashes (Workspace/Gallery tiles
    // kept while in Image mode) must survive Image-mode LoadReplace / Next.
    clearInteractionState();
    if (m_undoStack) {
        m_undoStack->clear();
    }
    while (!m_items.isEmpty()) {
        destroyCanvasItem(m_items.last());
    }
    // Do not m_scene->clear() — that would delete stashed items if any were
    // still parented (they are not). Scene may hold no items; that is fine.
    m_mouseInfo = {};
    emit mouseInfoChanged(m_mouseInfo);
}

void ImageView::clearWorkspace()
{
    // Full session/canvas wipe including mode stashes.
    clearLiveCanvas();
    discardStashedWorkspace();
    discardStashedGallery();
    m_pendingScenePos.clear();
    m_pendingWorkspacePaths.clear();
    m_pendingRestoreStates.clear();
    m_pendingSessionBinds.clear();
    m_pendingSessionIndexByPath.clear();
    m_galleryDecodeScheduled.clear();
    m_galleryDecodeFailed.clear();
    m_classicPath.clear();
    if (m_scene) {
        m_scene->blockSignals(true);
        m_scene->clear();
        m_scene->blockSignals(false);
    }
}

void ImageView::prepareImageModeCanvas()
{
    m_undoStack->clear();
    m_scene->clearSelection();
    resetTransform();
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
    // Drop large Gallery/Workspace scene rects so fitInView centres cleanly.
    m_scene->setSceneRect(QRectF());
    m_fitMode = true;
    m_fillMode = false;
}

void ImageView::prepareGalleryCanvas()
{
    // Drop Image-mode fit transforms and prior layout scene rects so the previous
    // frame does not linger under the new packing (visible "ghost" between switches).
    m_undoStack->clear();
    m_scene->clearSelection();
    resetTransform();
    if (horizontalScrollBar()) {
        horizontalScrollBar()->setValue(0);
    }
    if (verticalScrollBar()) {
        verticalScrollBar()->setValue(0);
    }
    m_scene->setSceneRect(QRectF());
    m_fitMode = true;
    m_fillMode = false;
    // Force a blank pass before items are re-packed.
    viewport()->update();
}

void ImageView::setViewMode(ViewMode mode)
{

    if (mode == m_viewMode) {
        return;
    }

    if (m_cropMode) {
        leaveCropModeInternal(false);
    }

    const ViewMode previous = m_viewMode;
    if (previous == ViewMode::Gallery) {
        m_gallery.clearHoverPath();
        m_gallery.setSelectionAnchor(nullptr);
        setDragMode(QGraphicsView::NoDrag);
        // Stop deferred packs immediately — a pending 0ms debounce after
        // scrollbar/thumb resize must not re-enter applyLayout while we tear down.
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
        m_gallery.setPendingRestore(false);
        m_applyingLayout = false;
        // Gallery → Image: keep scroll/centre snapshot from snapshotGalleryViewport()
        // (called just before setViewMode) so return-to-Gallery can restore it.
        // Any other leave path drops the snapshot.
        if (mode != ViewMode::Image) {
            m_gallery.clearScroll();
            m_gallery.clearViewCenter();
        }
    }

    if (previous == ViewMode::Workspace && mode != ViewMode::Workspace) {
        snapshotWorkspace();
    }

    if (mode == ViewMode::Image) {
        if (previous == ViewMode::Gallery) {
            // Keep tiles + decoded pixels for a fast return to Gallery.
            stashGalleryItems();
        } else if (previous == ViewMode::Workspace) {
            // Keep free-form tiles + pixels + view for a fast return to Workspace.
            // snapshotWorkspace() already ran above for durable state backup.
            stashWorkspaceItems();
        }
        m_viewMode = ViewMode::Image;
        m_layoutMode = LayoutMode::FreeForm;
        viewport()->update();
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
        m_applyingLayout = false;
        prepareImageModeCanvas();
        // Prefer explicit classic path. Do not pick m_items.first() when leaving
        // Gallery — that re-decodes a random tile before setCurrentIndex loads
        // the real target (double clear + decode spike).
        const QString path = m_classicPath;
        // Clear live canvas only — do not discard stashes.
        clearInteractionState();
        if (m_undoStack) {
            m_undoStack->clear();
        }
        while (!m_items.isEmpty()) {
            destroyCanvasItem(m_items.last());
        }
        m_pendingScenePos.clear();
        m_pendingWorkspacePaths.clear();
        m_pendingRestoreStates.clear();
        m_classicPath.clear();
        if (m_scene) {
            m_scene->blockSignals(true);
            m_scene->clear();
            m_scene->blockSignals(false);
        }
        m_mouseInfo = {};
        emit mouseInfoChanged(m_mouseInfo);
        if (!path.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return;
    }

    if (mode == ViewMode::Workspace) {
        m_workspace.enter(static_cast<int>(previous));
        return;
    }

    // Gallery
    prepareGalleryCanvas();
    m_viewMode = ViewMode::Gallery;
    if (m_layoutMode == LayoutMode::FreeForm) {
        m_layoutMode = LayoutMode::Masonry;
    }
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
        item->setItemOpacity(1.0);
        item->setItemRotation(0.0);
    }
    if (!m_items.isEmpty()) {
        applyLayout(GalleryPackReason::EnterGallery);
    }
    emit statusChanged();
}
