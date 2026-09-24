// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWMOTIONGEOMETRY_H
#define SLIDESHOWMOTIONGEOMETRY_H

#include "slideshow/slideshowtypes.h"
#include "view/viewtransform.h"

#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QtGlobal>

/**
 * Pure Ken Burns / PanScan cover destination geometry and bias-path picks.
 * No QObject, no viewport widget — callers pass CSS pixel sizes and settings.
 */
namespace SlideshowMotionGeometry {

/** PanZoom / dwell bias endpoints and derived travel direction. */
struct BiasPath {
    QPointF a{-1.0, -1.0};
    QPointF b{1.0, 1.0};
    QPointF travelDir{0.0, 1.0};
    qreal motionSign = 1.0;
};

/**
 * Destination rect in viewport CSS space for a motion-cover blit of an image
 * of size (@p iw, @p ih) into (@p vw, @p vh).
 *
 * @p baseScale is Fit/Fill/Actual from SlideshowAtlasPolicy::zoomBaseScale.
 * @p dwellBiasValid when true, @p biasA/@p biasB are taken as given for PanZoom;
 * otherwise default corner/edge biases are derived from @p pathForSeed when
 * biases are still the sentinel (-1,-1)/(1,1).
 */
QRectF coverDestRect(SlideshowMotion motion, qreal baseScale, qreal panZoomFactor,
                     qreal iw, qreal ih, int vw, int vh, qreal motionT,
                     QPointF biasA, QPointF biasB, bool dwellBiasValid,
                     const QString &pathForSeed);

/**
 * Corner/edge bias pair from @p seed (same table as historical ImageView path).
 * Always succeeds; endpoints are distinct after collision repairs.
 */
BiasPath geometricBiasPath(uint seed);

/**
 * Attention-centred bias path from normalized focus @p att01 in [0,1]².
 * Returns false when attention is near centre (caller should use geometry).
 */
bool attentionBiasPath(const QPointF &att01, uint seed, BiasPath *out);

/** True when atlas vs image aspect differs by more than @p threshold (default 0.03). */
bool aspectMismatch(qreal atlasW, qreal atlasH, qreal imageW, qreal imageH,
                    qreal threshold = 0.03);

/** Uniform cover scale: max(dest/native) per axis (Full content into dest). */
inline qreal coverDevicePixelScale(const QSizeF &dest, const QSize &native)
{
    return ViewTransform::coverScale(dest.width(), dest.height(),
                                     qreal(native.width()), qreal(native.height()));
}

inline qreal coverAxisScaleX(const QSizeF &dest, const QSize &native)
{
    return dest.width() / qMax(1.0, qreal(native.width()));
}

inline qreal coverAxisScaleY(const QSizeF &dest, const QSize &native)
{
    return dest.height() / qMax(1.0, qreal(native.height()));
}

inline int atlasBudgetPx(int viewportW, int viewportH)
{
    return qMax(viewportW, viewportH) * 2;
}


/** Bias space is [-1, 1] per axis (corner/edge table + attention map). */
inline qreal clampBiasCoord(qreal v)
{
    return qBound(-1.0, v, 1.0);
}

inline QPointF clampBiasPoint(QPointF p)
{
    p.setX(clampBiasCoord(p.x()));
    p.setY(clampBiasCoord(p.y()));
    return p;
}

/** PanZoom end scale relative to zoom base (must be finite and > 0). */
inline qreal clampPanZoomFactor(qreal factor)
{
    if (!qIsFinite(factor) || factor <= 0.0) {
        return 1.12; // default when invalid
    }
    return factor;
}

} // namespace SlideshowMotionGeometry

#endif // SLIDESHOWMOTIONGEOMETRY_H
