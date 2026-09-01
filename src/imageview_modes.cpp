// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QScrollBar>
#include <QUndoStack>
#include <QTimer>


void ImageView::stopDeferredPacking()
{
    if (m_layoutDebounceTimer) {
        m_layoutDebounceTimer->stop();
    }
    m_applyingLayout = false;
}

void ImageView::setActiveMode(ViewMode mode, LayoutMode layout)
{
    m_viewMode = mode;
    m_layoutMode = layout;
    viewport()->update();
}

void ImageView::clearPendingLoads()
{
    m_pendingScenePos.clear();
    m_pendingWorkspacePaths.clear();
    m_pendingRestoreStates.clear();
}

void ImageView::clearSceneKeepingStashes()
{
    if (!m_scene) {
        return;
    }
    m_scene->blockSignals(true);
    m_scene->clear();
    m_scene->blockSignals(false);
}

void ImageView::scheduleReplaceLoad(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    scheduleImageLoad(path, LoadReplace);
}

void ImageView::scheduleRestoreLoad(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    scheduleImageLoad(path, LoadRestore);
}

void ImageView::applyModeFlagsToLiveItems()
{
    for (ImageItem *item : m_items) {
        if (item) {
            applyItemModeFlags(item);
        }
    }
}

void ImageView::ensurePrimarySelection()
{
    if (m_items.isEmpty() || !m_scene) {
        return;
    }
    if (!m_scene->selectedItems().isEmpty()) {
        return;
    }
    if (ImageItem *first = m_items.first()) {
        first->setSelected(true);
    }
}

void ImageView::applyToolDragMode()
{
    if (m_tool == Tool::Select) {
        setDragMode(QGraphicsView::RubberBandDrag);
    } else {
        setDragMode(QGraphicsView::NoDrag);
    }
}

void ImageView::clearFitFillModes()
{
    m_fitMode = false;
    m_fillMode = false;
}

void ImageView::enableFitMode()
{
    m_fitMode = true;
    m_fillMode = false;
}



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
    clearClassicPath();
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
        m_gallery.onLeave(static_cast<int>(mode));
    }

    if (previous == ViewMode::Workspace && mode != ViewMode::Workspace) {
        m_workspace.onLeave(static_cast<int>(mode));
    }

    if (mode == ViewMode::Image) {
        m_image.enter();
        return;
    }

    if (mode == ViewMode::Workspace) {
        m_workspace.enter(static_cast<int>(previous));
        return;
    }

    // Gallery — sole entry is GalleryController::enter (also used by enterGallery).
    // setViewMode(Gallery) is not used by MainWindow; keep a safe path that
    // restores stash and packs rather than a second divergent implementation.
    LayoutMode layout = m_layoutMode;
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    m_gallery.enter(static_cast<int>(layout));
}

