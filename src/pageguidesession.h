// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PAGEGUIDESESSION_H
#define PAGEGUIDESESSION_H

#include <QRectF>
#include <QSizeF>

/**
 * Print page-guide overlay geometry and resize-handle drag.
 * Handle indices match ImageView::pageGuideHandleAt (-1 = none).
 */
struct PageGuideSession {
    bool visible = false;
    /** Scene px from printer page; empty → A4 at default DPI. */
    QSizeF size;
    /** When valid, overrides centred-at-origin placement. */
    QRectF rect;
    bool selected = false;
    int hoverHandle = -1;
    int dragHandle = -1;
    QRectF dragStartRect;

    void clearDrag()
    {
        dragHandle = -1;
        dragStartRect = {};
    }

    void clear()
    {
        visible = false;
        size = {};
        rect = {};
        selected = false;
        hoverHandle = -1;
        clearDrag();
    }
};

#endif // PAGEGUIDESESSION_H
