// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "toolpolicy.h"
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
    m_layoutApply.clear();
}

void ImageView::setActiveMode(ViewMode mode, LayoutMode layout)
{
    m_viewMode = mode;
    m_layout.setMode(layout);
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




void ImageView::invalidateSessionLoads()
{
    // New Open / History session: cancel every in-flight decode and drop the
    // live canvas so a late soft/PreferCache for the previous session cannot
    // paint over the first image of the new set.
    m_displayPipeline.loadGate().bumpGeneration();
    if (m_displayPipeline.tileCoordinator()) {
        m_displayPipeline.tileCoordinator()->clearPreferCancelled();
    }
    m_displayPipeline.loadGate().clearPending();
    m_displayPipeline.gallerySoftResetAll();
    m_slideshow.phase().clearRasterQueues();
    m_slideshow.phase().bumpPhaseUpgradeGeneration();
    m_slideshow.dwell().bumpAtlasRebuildGeneration();
    m_slideshow.phase().bumpToAtlasRebuildGeneration();
    // Drop logical-size memory so the size-first gate re-probes (stale square
    // stand-ins must not skip resolve on the next open).
    m_sizeBook.clear();
    m_gallerySizeResolve.cancel();
    if (isImageMode()) {
        clearLiveCanvas();
        m_image.clearClassicPath();
    }
    if (isGalleryMode()) {
        clearLiveCanvas();
    }
    // Drop process tile RAM and host underlays retained across sessions
    // (old archive paths / LQIP samples). clearWorkspace does the same for
    // Workspace; Gallery Open only hits invalidateSessionLoads.
    m_tileNeighborPrefetch.clear();
    // Stashed Gallery/Workspace items can still hold SharedPathTiles.
    m_displayPipeline.dropAllTileLodSessions();
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
    if (!m_displayPipeline.loadGate().takePendingWorkspacePath(path)) {
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
    m_displayPipeline.scheduleImageLoad(path, LoadReplace);
}

void ImageView::scheduleRestoreLoad(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_displayPipeline.scheduleImageLoad(path, LoadRestore);
}

void ImageView::applyModeFlagsToLiveItems()
{
    for (ImageItem *item : m_items) {
        if (item) {
            applyItemModeFlags(item);
        }
    }
}


void ImageView::applyToolDragMode()
{
    setDragMode(ToolPolicy::workspaceRubberBand(m_tool)
                    ? QGraphicsView::RubberBandDrag
                    : QGraphicsView::NoDrag);
}





void ImageView::clearInteractionState()
{
    m_itemInteract.clear();
    m_groupXform.clear();
    m_gallery.clearChrome();
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
    m_chrome.clearMouseInfo();
    emit mouseInfoChanged(m_chrome.currentMouseInfo());
}

void ImageView::clearWorkspace()
{
    // Full session/canvas wipe including mode stashes and the durable
    // Workspace snapshot so a subsequent enter() does not resurrect the
    // previous arrangement (project load, session replace).
    clearLiveCanvas();
    m_workspace.discardStash();
    m_gallery.discardStash();
    m_workspace.savedItems().clear();
    m_displayPipeline.loadGate().clearPending();
    m_bindBook.clear();
    m_pendingAppearance.clear();
    m_displayPipeline.gallerySoftResetAll();
    m_sizeBook.clear();
    m_gallerySoftBook.setDeferPopulate(false);
    m_gallerySizeResolve.cancel();
    ImageCache::clear();
    m_tileNeighborPrefetch.clear();
    m_displayPipeline.dropAllTileLodSessions();
    tilelod::TileLodRegistry::instance().invalidateAll();
    ThumtooCache::clearSessionReplaceMemos();
    pathOrderClear();
    // Path-keyed placement is legacy for unbound tiles only; drop it so a
    // project load cannot inherit stale poses from a previous session.
    m_itemWorld.pathBook().clear();
    m_image.clearClassicPath();
    // Invalidate in-flight LoadReplace so a prior Image-mode decode cannot
    // seed the empty Workspace after this wipe (first-path unbound tile).
    m_displayPipeline.loadGate().bumpGeneration();
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
    m_framing.setFitOnly();
}



void ImageView::setViewMode(ViewMode mode)
{

    if (mode == m_viewMode) {
        return;
    }

    if (m_cropCtrl.session().active()) {
        m_cropCtrl.leaveCropModeInternal(false);
    }
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.setAttentionMode(false);
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
    LayoutMode layout = m_layout.currentMode();
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    m_gallery.enter(static_cast<int>(layout));
}


// --- from imageview_layout.cpp (modes) ---

void ImageView::applyItemModeFlags(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Strict separation:
    //   Workspace → movable + handles
    //   Gallery   → selectable only (open on click), no chrome
    //   Image     → static, no selection chrome
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    } else if (isGalleryMode()) {
        item->setGallerySelectable(true);
        item->setScaleHandlesEnabled(false);
    } else {
        item->setGalleryCellSize({});
        item->setInteractive(false);
        item->setScaleHandlesEnabled(false);
    }
}

