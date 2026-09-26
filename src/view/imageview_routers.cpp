// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with view/ ownership.

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "util/biltoo_logging.h"
#include "view/viewmodeflags.h"
#include "image/toolpolicy.h"
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>

// --- from src/imageview_paint.cpp ---
void ImageView::paintEvent(QPaintEvent *event)
{
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    // Perf timing lives on HudChrome (BILTOO_PERF / THUMTOO_DEBUG).
    m_hud.runTimedPaint([this, event]() {
        QGraphicsView::paintEvent(event);
    });
}

// --- from src/imageview_paint_background.cpp ---
void ImageView::paintCanvasBackground(QPainter *painter, const QRectF &rect,
                                      qreal viewScale)
{
    m_shell.paintCanvasBackground(painter, rect, viewScale);
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    GUI_BUDGET("ImageView::drawBackground");
    m_shell.paintBackground(painter, rect, transform().m11());
}

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    GUI_BUDGET("ImageView::drawForeground");
    m_shell.paintForeground(painter, rect);
}

// --- Background settings: ViewShellChrome owns material mutators ---

// --- from src/imageview_input.cpp ---
void ImageView::updateMouseInfo(const QPoint &viewPos)
{
    m_shell.updateMouseInfo(viewPos);
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    m_shell.handleWheel(event);
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    m_shell.handleResize();
}

bool ImageView::setHoverEdge(EdgeZone zone)
{
    return m_image.setHoverEdge(edgeZoneToPolicy(zone));
}

// --- from src/imageview_input_events.cpp ---
void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (m_shell.handleMousePress(event)) {
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseMove(event)) {
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::restoreToolCursor()
{
    m_shell.restoreToolCursor();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseRelease(event)) {
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_shell.handleKeyPress(event)) {
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseDoubleClick(event)) {
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    m_shell.handleLeave();
    QGraphicsView::leaveEvent(event);
}

bool ImageView::viewportEvent(QEvent *event)
{
    if (m_shell.handleViewportEvent(event)) {
        return event->isAccepted();
    }
    return QGraphicsView::viewportEvent(event);
}

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    m_shell.dragEnterEvent(event);
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    m_shell.dragMoveEvent(event);
}

void ImageView::dropEvent(QDropEvent *event)
{
    m_shell.dropEvent(event);
}


// --- from imageview_modes.cpp ---

void ImageView::setActiveMode(ViewMode mode, LayoutMode layout)
{
    m_viewMode = mode;
    hostLayout().setMode(layout);
    m_shell.applyModeViewportPolicy(static_cast<int>(mode));
    // Mode-appropriate default tool (Select for Gallery/Workspace, Pan for Image).
    setTool(ViewInteraction::defaultToolForMode(static_cast<int>(mode)));
}

void ImageView::setTool(Tool tool)
{
    m_interaction.setTool(tool);
    if (!hostChrome().isPanning()) {
        setCursor(ToolPolicy::cursorFor(m_interaction.tool()));
    }
    if (isWorkspaceMode()) {
        m_workspace.applyToolDragMode();
    }
    // Gallery keeps RubberBandDrag from enterGallery; Image mode left-drag
    // policy is unchanged in Phase A (still driven by chrome preference).
}

void ImageView::takePendingWorkspacePath(const QString &path)
{
    if (!m_displayPipeline->loadGate().takePendingWorkspacePath(path)) {
        return;
    }
    // Status bar / HUD pending count (even when the caller also emits).
    emit statusChanged();
}
void ImageView::clearLiveCanvas()
{
    m_workspace.clearLiveCanvas();
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


// --- Tool / nav shell (was imageview_view.cpp) ---

