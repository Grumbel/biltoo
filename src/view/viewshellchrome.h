// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWSHELLCHROME_H
#define VIEWSHELLCHROME_H

#include "view/viewportchrome.h"
#include "view/canvasbackground.h"

class ImageView;
class QMouseEvent;

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

private:
    ImageView *m_view = nullptr;
    ViewportChrome m_viewport;
    CanvasBackground m_canvasBg;
};

#endif // VIEWSHELLCHROME_H
