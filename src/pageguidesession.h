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

    /** Begin resize from handle with starting page rect in scene space. */
    void beginResize(int handle, const QRectF &startRect)
    {
        dragHandle = handle;
        dragStartRect = startRect;
    }

    void endResize()
    {
        clearDrag();
    }

    /** @return true when selection flag changed. */
    bool setSelected(bool on)
    {
        if (selected == on) {
            return false;
        }
        selected = on;
        if (!on) {
            setHoverHandle(-1);
            clearDrag();
        }
        return true;
    }

    /** @return true when hover handle index changed. */
    bool setHoverHandle(int handle)
    {
        if (hoverHandle == handle) {
            return false;
        }
        hoverHandle = handle;
        return true;
    }

    /** @return true when visibility changed. */
    bool setVisible(bool on)
    {
        if (visible == on) {
            return false;
        }
        visible = on;
        if (!on) {
            selected = false;
            setHoverHandle(-1);
            clearDrag();
        }
        return true;
    }

    /** @return true when page size changed. */
    bool setSize(const QSizeF &sz)
    {
        if (size == sz) {
            return false;
        }
        size = sz;
        return true;
    }

    /** @return true when page rect changed. */
    bool setRect(const QRectF &r)
    {
        if (rect == r) {
            return false;
        }
        rect = r;
        return true;
    }

    /** @return true when page rect or size changed. */
    bool setPage(const QRectF &r)
    {
        const QSizeF sz = r.size();
        if (rect == r && size == sz) {
            return false;
        }
        rect = r;
        size = sz;
        return true;
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
