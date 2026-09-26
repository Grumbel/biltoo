// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWSHELLCHROME_H
#define VIEWSHELLCHROME_H

#include "view/viewportchrome.h"
#include "view/canvasbackground.h"

class ImageView;
class QMouseEvent;
class QPoint;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QPainter;
class QKeyEvent;
class QWheelEvent;
class QResizeEvent;
class QEvent;
class QRectF;

/**
 * Shell view chrome: transient viewport pointer state + canvas materials +
 * multi-mode pan interaction. Mode tools / edge zones live on controllers;
 * this bag stays with the QGraphicsView shell.
 */
class ViewShellChrome
{
public:
    void bindView(ImageView *view) { m_view = view; }

    ViewportChrome &viewport() { return m_viewport; }
    const ViewportChrome &viewport() const { return m_viewport; }

    CanvasBackground &canvasBg() { return m_canvasBg; }
    const CanvasBackground &canvasBg() const { return m_canvasBg; }

    /** Middle-button / Alt+left / Pan-tool pan (any mode). */
    bool tryMousePressPan(QMouseEvent *event);
    bool tryMouseMovePan(QMouseEvent *event);
    bool tryMouseReleasePan(QMouseEvent *event);

    /** Hit-test under cursor → status-bar mouse info. */
    void updateMouseInfo(const QPoint &viewPos);
    /** Pointer left the viewport: clear transient mouse info. */
    void onLeave();
    /** Restore cursor for the current Workspace tool (after pan / chrome drag). */
    void restoreToolCursor();

    /** External / internal path drag-drop (accept + filesDropped emit). */
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dropEvent(QDropEvent *event);
    /**
     * QOpenGLWidget viewport drag/drop: forward DragEnter/Move/Drop to the view
     * shell handlers. @return true when the event type was handled (accepted flag
     * is on the event). ImageView::viewportEvent is a thin router.
     */
    bool handleViewportEvent(QEvent *event);

    /** Empty-canvas open/drop invite + edge-zone captions (viewport device pixels). */
    void paintEmptySessionInvite(QPainter &painter) const;
    /** Pinned HUD panels, action flash, centre progress (viewport device pixels). */
    void paintHudPanels(QPainter &painter) const;
    /**
     * Viewport-device-pixel overlay pass (text rubber, crop/attention, workspace
     * chrome, slideshow letterbox/seekbar, edges, HUD). Called from paintForeground
     * after identity transform.
     */
    void paintViewportOverlays(QPainter &painter);
    /**
     * Full QGraphicsView drawForeground body: scene chrome (page guide, text,
     * gallery frames), bare-Gallery early-out, DPR identity transform, then
     * paintViewportOverlays.
     */
    void paintForeground(QPainter *painter, const QRectF &rect);
    /** Canvas materials (checker / tile / content blur). */
    void paintCanvasBackground(QPainter *painter, const QRectF &rect, qreal viewScale);
    /** Full drawBackground: canvas + gallery placeholders + page paper. */
    void paintBackground(QPainter *painter, const QRectF &rect, qreal viewScale);
    /** Re-apply AsNeeded scrollbar policies after fit/sceneRect (stale range fix). */
    void refreshScrollBarGeometry();
    /** Gallery → BoundingRect updates; Image/Workspace → FullViewportUpdate. */
    void applyModeViewportPolicy(int viewMode);

    /**
     * Input dispatch chains (controllers + pan). @return true when the event
     * was handled and QGraphicsView default must not run.
     */
    bool handleMousePress(QMouseEvent *event);
    bool handleMouseMove(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool handleKeyPress(QKeyEvent *event);
    bool handleKeyRelease(QKeyEvent *event);
    bool handleMouseDoubleClick(QMouseEvent *event);
    void handleLeave();

    /** Wheel: Gallery zoom/scroll first, else Image zoom-about-cursor. */
    void handleWheel(QWheelEvent *event);
    /**
     * Post-QGraphicsView resize: mode-specific quality climb / pack / framing.
     * Caller must invoke QGraphicsView::resizeEvent first.
     */
    void handleResize();

    /**
     * Canvas material mutators (Preferences / project / session view override).
     * Update CanvasBackground, load tile pixmaps when needed, apply QGraphicsView
     * brush + viewport update. ImageView public setters are thin routers here.
     */
    void setBackgroundColor(const QColor &color);
    void setBackgroundColorAlt(const QColor &color);
    void setBackgroundPattern(BackgroundPattern pattern);
    void setCheckerboardWorkspaceOnly(bool on);
    void setWorkspaceBackground(const WorkspaceBackground &bg);
    void setWorkspaceBackgroundShowDefault(bool on);
    void setViewBackground(const WorkspaceBackground &bg);

    /**
     * Centre progress HUD + viewport-update-mode policy (empty canvas / size-resolve
     * need FullViewportUpdate so the panel paints). ImageView public API is thin.
     */
    void setCentreProgress(const QString &title, const QString &detail = QString());
    void clearCentreProgress();

    /**
     * Content-edit mark chrome on all live tiles + scene/viewport refresh.
     * ImageView public setter is a thin router.
     */
    void setContentEditMarksVisible(bool on);

private:
    /** After a material change: update viewport (and solid brush when primary changes). */
    void refreshViewportAfterMaterialChange(bool updateSolidBrush = false);

    ImageView *m_view = nullptr;
    ViewportChrome m_viewport;
    CanvasBackground m_canvasBg;
};

#endif // VIEWSHELLCHROME_H
