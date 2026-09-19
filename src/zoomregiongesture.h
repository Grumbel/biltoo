// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZOOMREGIONGESTURE_H
#define ZOOMREGIONGESTURE_H

#include "viewtransform.h"

#include <QPoint>
#include <QRect>

#include <QRubberBand>

/**
 * One-shot rubber-band zoom (Z tool): armed until drag completes or Esc.
 * QRubberBand widget is owned by ImageView (QObject parent).
 */
struct ZoomRegionGesture {
    static constexpr int kMinRubberPx = 8;

    bool armed = false;
    bool dragging = false;
    QPoint origin;
    QRubberBand *rubberBand = nullptr;

    void clearDrag()
    {
        dragging = false;
        origin = {};
    }

    /** @return true when armed flag changed to true. */
    bool arm()
    {
        if (armed) {
            return false;
        }
        armed = true;
        return true;
    }

    /** @return true when was armed. */
    bool disarm()
    {
        if (!armed && !dragging) {
            return false;
        }
        armed = false;
        clearDrag();
        // rubberBand lifetime stays with ImageView
        return true;
    }

    void beginDrag(const QPoint &pos)
    {
        dragging = true;
        origin = pos;
    }

    void endDrag() { dragging = false; }

    void setRubberBand(QRubberBand *band) { rubberBand = band; }

    void clearRubberBand() { rubberBand = nullptr; }

    void hideRubber()
    {
        if (rubberBand) {
            rubberBand->hide();
        }
    }

    bool isActive() const { return armed || dragging; }

    bool rubberSignificant(const QRect &viewRect) const
    {
        return ViewTransform::significantRubber(viewRect, kMinRubberPx);
    }

    bool isArmed() const { return armed; }

    bool isDragging() const { return dragging; }

    bool hasRubberBand() const { return rubberBand != nullptr; }

    QPoint originPos() const { return origin; }

    void showRubberAt(const QPoint &pos)
    {
        if (!rubberBand) {
            return;
        }
        rubberBand->setGeometry(QRect(pos, QSize()));
        rubberBand->show();
    }

    void updateRubberTo(const QPoint &pos)
    {
        if (!rubberBand) {
            return;
        }
        rubberBand->setGeometry(ViewTransform::rubberRect(origin, pos));
    }
};

#endif // ZOOMREGIONGESTURE_H
