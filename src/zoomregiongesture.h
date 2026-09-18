// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZOOMREGIONGESTURE_H
#define ZOOMREGIONGESTURE_H

#include <QPoint>

class QRubberBand;

/**
 * One-shot rubber-band zoom (Z tool): armed until drag completes or Esc.
 * QRubberBand widget is owned by ImageView (QObject parent).
 */
struct ZoomRegionGesture {
    bool armed = false;
    bool dragging = false;
    QPoint origin;
    QRubberBand *rubberBand = nullptr;

    void clearDrag()
    {
        dragging = false;
        origin = {};
    }

    void disarm()
    {
        armed = false;
        clearDrag();
        // rubberBand lifetime stays with ImageView
    }
};

#endif // ZOOMREGIONGESTURE_H
