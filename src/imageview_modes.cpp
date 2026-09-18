// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imagecache.h"
#include <QDebug>
#include "imageitem.h"
#include "imageloader.h"
#include "thumtoocache.h"
#include "tilelod/tile_lod_registry.hpp"
#include "tilelod/tile_lod_controller.hpp"

#include <QScrollBar>
#include <QUndoStack>
#include <QTimer>
#include <QSet>


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
    m_layout.mode = layout;
    // Gallery: BoundingRect — FullViewportUpdate repaints every tile on each
    // scroll/zoom tick and is unusable with large soft bitmaps. Soft upgrades
    // must call item->update() (installDisplayPixels already does).
    // Image/Workspace: FullViewportUpdate for HUD/chrome.
    if (mode == ViewMode::Gallery) {
        setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    } else {
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    }
    if (viewport()) {
        viewport()->update();
    }
}

void ImageView::clearPendingLoads()
{
    m_loadGate.clearPending();
}

void ImageView::invalidateGalleryDecodes()
{
    // Drop scheduled markers and pending path counts so late LoadAdd results
    // cannot create tiles after leaving Gallery. Bump generation so in-flight
    // pool jobs are rejected in onImageLoaded.
    gallerySoftResetAll();
    m_loadGate.clearPendingWorkspacePaths();
    m_loadGate.bumpGeneration();
}

void ImageView::invalidateSessionLoads()
{
    m_appearanceSeedAttempted.clear();
    // New Open / History session: cancel every in-flight decode and drop the
    // live canvas so a late soft/PreferCache for the previous session cannot
    // paint over the first image of the new set.
    m_loadGate.bumpGeneration();
    clearPendingLoads();
    gallerySoftResetAll();
    m_ss.rasterInflight.clear();
    m_ss.rasterPending.clear();
    m_ss.phaseUpgradeGeneration++;
    m_ssDwell.atlasRebuildGeneration++;
    m_ss.toAtlasRebuildGeneration++;
    // Drop logical-size memory so the size-first gate re-probes (stale square
    // stand-ins must not skip resolve on the next open).
    m_imageSizeByPath.clear();
    m_provisionalSizePaths.clear();
    m_sizeProbeScheduled.clear();
    cancelGallerySizeResolve();
    if (isImageMode()) {
        clearLiveCanvas();
        clearClassicPath();
    }
    if (isGalleryMode()) {
        clearLiveCanvas();
    }
    // Drop process tile RAM and host underlays retained across sessions
    // (old archive paths / LQIP samples). clearWorkspace does the same for
    // Workspace; Gallery Open only hits invalidateSessionLoads.
    m_tileNeighborPrefetch.clear();
    tilelod::TileLodRegistry::instance().invalidateAll();
    ThumtooCache::clearSessionReplaceMemos();
    ImageCache::clear();
    if (m_pathRaster) {
        m_pathRaster->invalidateAll();
    }
    if (ThumtooCache::isAvailable()) {
        (void)ThumtooCache::bumpInterestEpoch();
    }
}

void ImageView::takePendingWorkspacePath(const QString &path)
{
    if (!m_loadGate.takePendingWorkspacePath(path)) {
        return;
    }
    // Status bar / HUD pending count (even when the caller also emits).
    emit statusChanged();
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
    m_framing.fitMode = false;
    m_framing.fillMode = false;
}

void ImageView::enableFitMode()
{
    m_framing.fitMode = true;
    m_framing.fillMode = false;
}



void ImageView::clearInteractionState()
{
    m_itemInteract.handleDragItem = nullptr;
    m_groupXform.scaleDrag = false;
    m_groupXform.rotateDrag = false;
    m_groupXform.handle = -1;
    m_groupXform.hoverHandle = -1;
    m_groupXform.dragItems.clear();
    m_groupXform.dragStartStates.clear();
    m_itemInteract.rotateItem = nullptr;
    m_itemInteract.rotating = false;
    m_itemInteract.dragItem = nullptr;
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
    // Snapshot unique pointers — m_items must never hold duplicates, but if it
    // does, destroying by index while mutating the list is unsafe.
    QList<ImageItem *> doomed;
    QSet<ImageItem *> seen;
    for (ImageItem *item : m_items) {
        if (item && !seen.contains(item)) {
            seen.insert(item);
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        destroyCanvasItem(item);
    }
    m_items.clear();
    // Do not m_scene->clear() — that would delete stashed items if any were
    // still parented (they are not). Scene may hold no items; that is fine.
    m_chrome.mouseInfo = {};
    emit mouseInfoChanged(m_chrome.mouseInfo);
}

void ImageView::clearWorkspace()
{
    // Full session/canvas wipe including mode stashes and the durable
    // Workspace snapshot so a subsequent enter() does not resurrect the
    // previous arrangement (project load, session replace).
    clearLiveCanvas();
    discardStashedWorkspace();
    discardStashedGallery();
    m_workspace.savedItems().clear();
    m_loadGate.pendingScenePos().clear();
    m_loadGate.clearPendingWorkspacePaths();
    m_loadGate.pendingRestoreStates().clear();
    m_pendingSessionBinds.clear();
    m_pendingSessionIndexByPath.clear();
    m_pendingSelectSessionIds.clear();
    gallerySoftResetAll();
    m_imageSizeByPath.clear();
    m_sizeProbeScheduled.clear();
    m_provisionalSizePaths.clear();
    m_galleryDeferPopulate = false;
    cancelGallerySizeResolve();
    ImageCache::clear();
    m_tileNeighborPrefetch.clear();
    tilelod::TileLodRegistry::instance().invalidateAll();
    ThumtooCache::clearSessionReplaceMemos();
    m_pathOrder.clear();
    m_sessionIdOrder.clear();
    // Path-keyed placement is legacy for unbound tiles only; drop it so a
    // project load cannot inherit stale poses from a previous session.
    m_itemStates.clear();
    clearClassicPath();
    // Invalidate in-flight LoadReplace so a prior Image-mode decode cannot
    // seed the empty Workspace after this wipe (first-path unbound tile).
    m_loadGate.bumpGeneration();
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
    {
        resetTransform();
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(0);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(0);
        }
    }
    // Drop large Gallery/Workspace scene rects so fitInView centres cleanly.
    m_scene->setSceneRect(QRectF());
    m_framing.fitMode = true;
    m_framing.fillMode = false;
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
    m_framing.fitMode = true;
    m_framing.fillMode = false;
    // Force a blank pass before items are re-packed.
    viewport()->update();
}

void ImageView::setViewMode(ViewMode mode)
{

    if (mode == m_viewMode) {
        return;
    }

    if (m_crop.mode) {
        leaveCropModeInternal(false);
    }
    if (m_attention.mode) {
        setAttentionMode(false);
    }

    const ViewMode previous = m_viewMode;
    if (previous == ViewMode::Gallery) {
        m_gallery.onLeave(static_cast<int>(mode));
    }

    if (previous == ViewMode::Workspace && mode != ViewMode::Workspace) {
        m_workspace.onLeave(static_cast<int>(mode));
    }

    // Sticky Fit/Fill/1:1 is Image-mode only.
    if (mode != ViewMode::Image) {
        releaseStickyZoom();
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
    LayoutMode layout = m_layout.mode;
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    m_gallery.enter(static_cast<int>(layout));
}

