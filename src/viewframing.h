// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWFRAMING_H
#define VIEWFRAMING_H

#include <QtGlobal>
#include <QPointF>
#include <QRectF>
#include <QtCore/qnamespace.h>

/**
 * Image-mode framing preferences and sticky/preserved navigation scale.
 */
enum class StickyZoomKind { Fit = 0, Fill = 1, Actual = 2 };

struct ViewFraming {
    bool fitMode = true;
    bool fillMode = false;
    bool stickyZoomEnabled = false;
    StickyZoomKind stickyZoomKind = StickyZoomKind::Fit;
    bool haveStickyPanAnchor = false;
    qreal stickyPanNormX = 0.5;
    qreal stickyPanNormY = 0.5;
    /** Free (non-sticky) nav: keep absolute view scale across images. */
    bool havePreservedViewScale = false;
    qreal preservedViewScale = 1.0;

    void clampStickyPanNorms()
    {
        stickyPanNormX = qBound(0.0, stickyPanNormX, 1.0);
        stickyPanNormY = qBound(0.0, stickyPanNormY, 1.0);
    }

    Qt::AspectRatioMode aspectMode() const
    {
        return fillMode ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
    }

    /** Capture sticky pan as norms of @p sceneCentre within @p itemBounds. */
    void setStickyPanFromScene(const QPointF &sceneCentre, const QRectF &itemBounds)
    {
        if (itemBounds.width() < 1.0 || itemBounds.height() < 1.0) {
            return;
        }
        stickyPanNormX = (sceneCentre.x() - itemBounds.left()) / itemBounds.width();
        stickyPanNormY = (sceneCentre.y() - itemBounds.top()) / itemBounds.height();
        clampStickyPanNorms();
        haveStickyPanAnchor = true;
    }

    QPointF sceneFromStickyPan(const QRectF &itemBounds) const
    {
        return QPointF(itemBounds.left() + stickyPanNormX * itemBounds.width(),
                       itemBounds.top() + stickyPanNormY * itemBounds.height());
    }
};

#endif // VIEWFRAMING_H
