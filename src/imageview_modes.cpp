// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "view/viewmodeflags.h"
#include "util/biltoo_thread.h"
#include "util/biltoo_logging.h"
#include "imageitem.h"

void ImageView::stopDeferredPacking()
{
    m_gallery.stopLayoutDebounceTimer();
    hostLayoutApply().clear();
}

void ImageView::setActiveMode(ViewMode mode, LayoutMode layout)
{
    m_viewMode = mode;
    hostLayout().setMode(layout);
    m_shell.applyModeViewportPolicy(static_cast<int>(mode));
}

void ImageView::invalidateSessionLoads()
{
    m_displayPipeline->invalidateSessionLoads();
}

void ImageView::takePendingWorkspacePath(const QString &path)
{
    if (!m_displayPipeline->loadGate().takePendingWorkspacePath(path)) {
        return;
    }
    // Status bar / HUD pending count (even when the caller also emits).
    emit statusChanged();
}

void ImageView::clearSceneKeepingStashes()
{
    m_image.clearSceneKeepingStashes();
}

void ImageView::scheduleReplaceLoad(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_displayPipeline->scheduleImageLoad(path, LoadReplace);
}

void ImageView::scheduleRestoreLoad(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_displayPipeline->scheduleImageLoad(path, LoadRestore);
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
    m_workspace.applyToolDragMode();
}

void ImageView::clearInteractionState()
{
    m_workspace.clearInteractionState();
}

void ImageView::clearLiveCanvas()
{
    m_workspace.clearLiveCanvas();
}

void ImageView::clearWorkspace()
{
    m_workspace.clearWorkspace();
}

void ImageView::prepareImageModeCanvas()
{
    m_image.prepareModeCanvas();
}

void ImageView::setViewMode(ViewMode mode)
{
    GUI_BUDGET("ImageView::setViewMode");
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
    m_image.maybeCaptureStickyPanOnLeave(static_cast<int>(previous));
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

    const LayoutMode layout = hostLayout().galleryEnterMode();
    setActiveMode(ViewMode::Gallery, layout);
    m_gallery.enter(static_cast<int>(layout), static_cast<int>(previous));
}

void ImageView::applyItemModeFlags(ImageItem *item)
{
    ViewModeFlags::applyToItem(item, static_cast<int>(m_viewMode));
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
    m_gallery.enterGallery(packagedLayout);
}


// --- Tool / nav shell (was imageview_view.cpp) ---

void ImageView::setTool(Tool tool)
{
    m_workspace.setTool(tool);
}

void ImageView::setImageModeNavigationEnabled(bool on)
{
    m_image.setImageModeNavigationEnabled(on);
}

void ImageView::setGalleryReturnAvailable(bool on)
{
    m_image.setGalleryReturnAvailable(on);
}

