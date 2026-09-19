// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWPORTCHROME_H
#define VIEWPORTCHROME_H

#include "imageview_types.h"

#include <QPoint>

/**
 * Transient viewport pointer chrome (hover, pan, last position).
 * Edge zone / tool stay on ImageView (mode coupling).
 */
struct ViewportChrome {
    ImageMouseInfo mouseInfo;
    QPoint lastMousePos;
    QPoint lastHoverViewPos;
    bool panning = false;
    bool imageModeLeftDragPan = true;

    QPoint panDeltaFrom(const QPoint &pos) const
    {
        return pos - lastMousePos;
    }

    void noteMousePos(const QPoint &pos)
    {
        lastMousePos = pos;
        lastHoverViewPos = pos;
    }

    void beginPan(const QPoint &pos)
    {
        panning = true;
        lastMousePos = pos;
    }

    void endPan() { panning = false; }

    void updatePanPos(const QPoint &pos) { lastMousePos = pos; }

    void setMouseInfo(const ImageMouseInfo &info) { mouseInfo = info; }

    void clearMouseInfo() { mouseInfo.clear(); }

    void setHoverViewPos(const QPoint &pos) { lastHoverViewPos = pos; }

    bool setImageModeLeftDragPan(bool on)
    {
        if (imageModeLeftDragPan == on) {
            return false;
        }
        imageModeLeftDragPan = on;
        return true;
    }
};

#endif // VIEWPORTCHROME_H
