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

    /** External / internal path drag-drop (accept + filesDropped emit). */
    void dragEnterEvent(QDragEnterEvent *event);
    void dragMoveEvent(QDragMoveEvent *event);
    void dropEvent(QDropEvent *event);

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

private:
    ImageView *m_view = nullptr;
    ViewportChrome m_viewport;
    CanvasBackground m_canvasBg;
};

#endif // VIEWSHELLCHROME_H
