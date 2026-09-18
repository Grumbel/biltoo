// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef GROUPTRANSFORMGEOMETRY_H
#define GROUPTRANSFORMGEOMETRY_H

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>

/**
 * Pure multi-select group scale/rotate chrome geometry.
 * Handle indices: 0–7 scale (TL…L), 8–11 rotate (T/R/B/L). No ImageView state.
 */
namespace GroupTransformGeometry {

constexpr qreal kScaleHitPx = 10.0;
constexpr qreal kRotateOffsetPx = 28.0;
constexpr qreal kRotateHitPx = 12.0;

/** True for rotate knobs (8–11). */
inline bool isRotateHandle(int handle)
{
    return handle >= 8 && handle <= 11;
}

/** Eight scale-grip centres for an axis-aligned view rect (TL… clockwise). */
void scaleHandlePoints(const QRect &viewRect, QPointF out[8]);

/** Four rotate-knob centres outside the view rect edges. */
void rotateHandlePoints(const QRect &viewRect, QPointF out[4],
                        qreal offsetPx = kRotateOffsetPx);

/**
 * Prefer rotate knobs over scale grips. Returns 0–11 or -1.
 */
int handleIndexAt(const QPoint &viewPos, const QRect &viewRect,
                  qreal scaleHitPx = kScaleHitPx, qreal rotateHitPx = kRotateHitPx,
                  qreal rotateOffsetPx = kRotateOffsetPx);

/** Opposite-corner/edge anchor for scale handle @p handle on scene AABB @p bounds. */
QPointF scaleAnchor(const QRectF &bounds, int handle);

/**
 * Scene scale factors from a scale-handle drag.
 * Edge: default axis stretch, Shift → uniform.
 * Corner: default uniform, Shift → free axes.
 * Factors clamped to [0.05, 20].
 */
struct ScaleFactors {
    qreal sx = 1.0;
    qreal sy = 1.0;
    QPointF anchor;
    bool valid = false;
};

ScaleFactors scaleFactorsFromDrag(const QPointF &scenePos, const QRectF &boundsStart,
                                  int handle, bool shift);

} // namespace GroupTransformGeometry

#endif // GROUPTRANSFORMGEOMETRY_H
