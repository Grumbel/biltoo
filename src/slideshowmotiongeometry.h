// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SLIDESHOWMOTIONGEOMETRY_H
#define SLIDESHOWMOTIONGEOMETRY_H

#include "slideshowtypes.h"

#include <QPointF>
#include <QRectF>
#include <QString>

/**
 * Pure Ken Burns / PanScan / static cover destination geometry for slideshow
 * paint. No QObject, no viewport widget — callers pass CSS pixel sizes and
 * motion settings.
 */
namespace SlideshowMotionGeometry {

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

} // namespace SlideshowMotionGeometry

#endif // SLIDESHOWMOTIONGEOMETRY_H
