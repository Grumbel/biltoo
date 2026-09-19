// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWFRAMING_H
#define VIEWFRAMING_H

#include <QtGlobal>
#include "viewtransform.h"
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
        stickyPanNormX = ViewTransform::clamp01(stickyPanNormX);
        stickyPanNormY = ViewTransform::clamp01(stickyPanNormY);
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

    /** @return true when either fit or fill flag changed. */
    bool clearFitFill()
    {
        if (!fitMode && !fillMode) {
            return false;
        }
        fitMode = false;
        fillMode = false;
        return true;
    }

    /** @return true when flags changed to fit-only. */
    bool setFitOnly()
    {
        if (fitMode && !fillMode) {
            return false;
        }
        fitMode = true;
        fillMode = false;
        return true;
    }

    /** @return true when flags changed to fill. */
    bool setFillMode()
    {
        if (fitMode && fillMode) {
            return false;
        }
        fitMode = true;
        fillMode = true;
        return true;
    }

    /**
     * Set both flags explicitly (e.g. sticky restore, slideshow zoom).
     * Slideshow Fill uses fit=false, fill=true — distinct from setFillMode().
     */
    bool setFitFillFlags(bool fit, bool fill)
    {
        if (fitMode == fit && fillMode == fill) {
            return false;
        }
        fitMode = fit;
        fillMode = fill;
        return true;
    }

    /** Arm fitMode only (leave fillMode unchanged). */
    bool armFit()
    {
        if (fitMode) {
            return false;
        }
        fitMode = true;
        return true;
    }

    /** Drop fitMode only (leave fillMode unchanged). */
    bool releaseFit()
    {
        if (!fitMode) {
            return false;
        }
        fitMode = false;
        return true;
    }

    /** @return true when sticky-zoom enabled flag changed. */
    bool setStickyZoomEnabled(bool on)
    {
        if (stickyZoomEnabled == on) {
            return false;
        }
        stickyZoomEnabled = on;
        return true;
    }

    /** @return true when sticky kind changed. */
    bool setStickyZoomKind(StickyZoomKind kind)
    {
        if (stickyZoomKind == kind) {
            return false;
        }
        stickyZoomKind = kind;
        return true;
    }

    /** Infer sticky kind from current fit/fill flags (1:1 is separate). */
    static StickyZoomKind kindFromFitFill(bool fitMode, bool fillMode)
    {
        if (fillMode) {
            return StickyZoomKind::Fill;
        }
        if (fitMode) {
            return StickyZoomKind::Fit;
        }
        return StickyZoomKind::Actual;
    }

    void syncStickyKindFromFitFill()
    {
        stickyZoomKind = kindFromFitFill(fitMode, fillMode);
    }

    void clearStickyPan() { haveStickyPanAnchor = false; }

    void clearPreservedViewScale() { havePreservedViewScale = false; }

    void setPreservedViewScale(qreal sx)
    {
        preservedViewScale = sx;
        havePreservedViewScale = true;
    }
};

#endif // VIEWFRAMING_H
