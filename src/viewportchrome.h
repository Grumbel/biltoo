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
};

#endif // VIEWPORTCHROME_H
