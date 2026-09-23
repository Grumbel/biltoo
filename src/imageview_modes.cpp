// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "image/toolpolicy.h"
#include "display/imagecache.h"
#include <QDebug>
#include "imageitem.h"
#include "host/imageloader.h"
#include "host/thumtoocache.h"
#include "util/biltoo_logging.h"
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
    m_displayPipeline.galleryDecodeResetAll();
    m_slideshow.phase().clearRasterQueues();
    m_slideshow.phase().bumpPhaseUpgradeGeneration();
    m_slideshow.dwell().bumpAtlasRebuildGeneration();
    m_slideshow.phase().bumpToAtlasRebuildGeneration();
    // Drop logical-size memory so the size-first gate re-probes (stale square
    // stand-ins must not skip resolve on the next open).
    m_sizeBook.clear();
    m_gallerySizeResolve.cancel();
    // Drop host size-probe FIFO + bump generation so previous-session Store
    // size callbacks cannot emit sizeReady or refill ImageCache after clear.
    ThumtooCache::cancelSizeProbes();
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
    // Do not QGraphicsScene::clear() — that deletes every item still parented to
    // the scene. Workspace/Gallery stashes are supposed to be off-scene, but if a
    // tile is still parented, clear() would free it and leave a dangling stash
    // pointer (Workspace re-enter → empty canvas).
    QSet<ImageItem *> keep;
    for (ImageItem *item : m_workspace.stashedItems()) {
        if (item) {
            keep.insert(item);
        }
    }
    for (ImageItem *item : m_gallery.stashedItems()) {
        if (item) {
            keep.insert(item);
        }
    }
    m_scene->blockSignals(true);
    const QList<QGraphicsItem *> all = m_scene->items();
    for (QGraphicsItem *gi : all) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (keep.contains(ii)) {
                m_scene->removeItem(ii);
                continue;
            }
        }
        m_scene->removeItem(gi);
        delete gi;
    }
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
    QSet<ImageItem *> protectedStash;
    for (ImageItem *item : m_workspace.stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    for (ImageItem *item : m_gallery.stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    // Snapshot unique pointers — m_items must never hold duplicates, but if it
    // does, destroying by index while mutating the list is unsafe.
    QList<ImageItem *> doomed;
    QSet<ImageItem *> seen;
    for (ImageItem *item : m_items) {
        if (item && !seen.contains(item)) {
            seen.insert(item);
            // Structural: never free a pointer that mode-stash still owns.
            if (protectedStash.contains(item)) {
                biltooModeDbg("clearLiveCanvas SKIP stashed ptr path=%s",
                              qPrintable(item->path()));
                Q_ASSERT_X(false, "clearLiveCanvas",
                           "live list holds a mode-stashed ImageItem* — ownership bug");
                continue;
            }
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
    m_displayPipeline.galleryDecodeResetAll();
    m_sizeBook.clear();
    m_galleryDecodeBook.setDeferPopulate(false);
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
    // SessionDocument::clear only wipes the seed book; sparse ItemWorld tables
    // must be cleared here so hasDurableAppearance cannot see stale ids.
    m_itemWorld.clearAppearance();
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
    // Mid-edit applied ContentXform must hit ItemWorld before leave (ECS #4).
    flushAppliedContentToItemWorld();
    biltooModeDbg("setViewMode %d→%d live=%d wstash=%d gstash=%d",
                  static_cast<int>(previous), static_cast<int>(mode),
                  itemCount(),
                  static_cast<int>(m_workspace.stashedItems().size()),
                  static_cast<int>(m_gallery.stashedItems().size()));

    // Mode switch = presentation systems over shared data stores (not three scenes).
    // Data: SessionDocument + ItemWorld + mode stashes / durable Workspace snapshots.
    // Presentation: one QGraphicsScene shows the *active mode only*.
    // Isolation: snapshot → detach live → set mode → attach destination.
    // Controllers may snapshot/stash; they must not be the only place that
    // clears the Image underlay (that omission caused Image→Workspace bleed).

    // 1) Snapshot previous presentation into mode stores.
    if (previous == ViewMode::Gallery) {
        m_gallery.onLeave(static_cast<int>(mode));
    } else if (previous == ViewMode::Workspace) {
        m_workspace.onLeave(static_cast<int>(mode));
    }
    // Image underlay is pure presentation (classicPath + session id are data).

    // 2) Detach ALL live presentation. After Gallery/Workspace stash, live is
    // already empty; after Image, destroy the underlay so it cannot bleed onto
    // Workspace/Gallery.
    // Image leave: capture view scale/pan *before* destroying the underlay so
    // free zoom (and sticky pan) survive Gallery/Workspace round-trips.
    if (previous == ViewMode::Image && !m_items.isEmpty()) {
        if (ImageItem *cur = targetItem()) {
            captureStickyPanAnchor(cur);
        } else {
            captureStickyPanAnchor(m_items.first());
        }
    }
    if (!m_items.isEmpty()) {
        biltooModeDbg("setViewMode detachLive residual=%d (prev=%d)",
                      itemCount(), static_cast<int>(previous));
        clearLiveCanvas();
    }
    biltooModeDbg("setViewMode afterDetach live=%d wstash=%d gstash=%d",
                  itemCount(),
                  static_cast<int>(m_workspace.stashedItems().size()),
                  static_cast<int>(m_gallery.stashedItems().size()));

    // 3) Active mode + 4) attach destination presentation.
    // Keep sticky zoom preference across leave/enter — it is an Image framing
    // preference, not presentation state tied to the underlay.
    if (mode == ViewMode::Image) {
        m_image.enter();
        return;
    }

    if (mode == ViewMode::Workspace) {
        setActiveMode(ViewMode::Workspace, LayoutMode::FreeForm);
        m_workspace.enter(static_cast<int>(previous));
        return;
    }

    LayoutMode layout = m_layout.currentMode();
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    setActiveMode(ViewMode::Gallery, layout);
    m_gallery.enter(static_cast<int>(layout), static_cast<int>(previous));
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

void ImageView::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts belong only to Gallery; FreeForm only to Workspace.
    if (mode == LayoutMode::FreeForm) {
        m_workspace.applyFreeFormLayout();
        return;
    }
    m_gallery.setLayoutMode(mode);
}

void ImageView::reloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        m_image.reloadFromDisk();
        return;
    }
    if (isGalleryMode()) {
        m_gallery.reloadFromDisk(relayoutGallery);
        return;
    }
    m_workspace.reloadFromDisk();
}

void ImageView::hardReloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        m_image.hardReloadFromDisk();
        return;
    }
    if (isGalleryMode()) {
        m_gallery.hardReloadFromDisk(relayoutGallery);
        return;
    }
    m_workspace.hardReloadFromDisk();
}

void ImageView::enterGallery(LayoutMode packagedLayout)
{
    // Sticky Fit/Fill/1:1 is Image-mode only.
    releaseStickyZoom();
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
    }
    if (m_viewMode == ViewMode::Gallery) {
        // Layout-only switch inside Gallery (no leave/enter of other modes).
        m_gallery.enter(static_cast<int>(packagedLayout),
                        static_cast<int>(ViewMode::Gallery));
        return;
    }
    // Full mode switch through the central path so Workspace/Image leave runs
    // and setActiveMode happens before GalleryController::enter.
    // Preserve requested layout for setViewMode's Gallery branch.
    m_layout.setMode(packagedLayout);
    setViewMode(ViewMode::Gallery);
}

// --- Tool / nav shell (was imageview_view.cpp) ---

void ImageView::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    setCursor(ToolPolicy::cursorFor(m_tool));
    // Workspace Select: rubber-band multi-select on empty drag (same as Gallery).
    // Pan / Zoom keep NoDrag (view gestures are handled in mouse handlers).
    if (isWorkspaceMode()) {
        setDragMode(ToolPolicy::workspaceRubberBand(m_tool)
                        ? QGraphicsView::RubberBandDrag
                        : QGraphicsView::NoDrag);
    }
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    if (!m_sessionNav.setImageModeNav(on)) {
        return;
    }
    if (!on && m_hoverEdge != EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    if (!m_sessionNav.setGalleryReturnAvailable(on)) {
        return;
    }
    if (!on && m_hoverEdge == EdgeZone::GalleryReturn) {
        clearHoverEdge();
    }
    viewport()->update();
}

