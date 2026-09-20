// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ZOOMREGIONGESTURE_H
#define ZOOMREGIONGESTURE_H

#include "viewtransform.h"

#include <QPoint>
#include <QRect>
#include <QRubberBand>
#include <QWidget>

/**
 * One-shot rubber-band zoom (Z tool): armed until drag completes or Esc.
 * QRubberBand widget is owned by the view (QObject parent).
 *
 * tryBeginPress / tryUpdateMove / tryEndRelease own the rubber-band lifecycle;
 * the view applies fitInView when tryEndRelease returns a significant rect.
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

    /**
     * Start a rubber-band drag. Ensures a QRubberBand parented to @p viewport.
     * @return true when the press was consumed.
     */
    bool tryBeginPress(const QPoint &pos, QWidget *viewport)
    {
        beginDrag(pos);
        if (!hasRubberBand() && viewport) {
            setRubberBand(new QRubberBand(QRubberBand::Rectangle, viewport));
        }
        showRubberAt(originPos());
        return true;
    }

    /** Update rubber geometry while dragging. @return true when active drag. */
    bool tryUpdateMove(const QPoint &pos)
    {
        if (!isDragging() || !hasRubberBand()) {
            return false;
        }
        updateRubberTo(pos);
        return true;
    }

    /**
     * End drag and hide rubber. If the rect is significant, writes it to
     * @p viewRectOut and returns true so the view can fitInView; otherwise
     * returns false (tiny click = cancel). Always ends the drag state.
     */
    bool tryEndRelease(const QPoint &pos, QRect *viewRectOut)
    {
        if (!isDragging()) {
            return false;
        }
        const QRect viewRect = ViewTransform::rubberRect(originPos(), pos);
        endDrag();
        hideRubber();
        if (!rubberSignificant(viewRect)) {
            if (viewRectOut) {
                *viewRectOut = {};
            }
            return false;
        }
        if (viewRectOut) {
            *viewRectOut = viewRect;
        }
        return true;
    }
};

#endif // ZOOMREGIONGESTURE_H
