// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PAGEGUIDEGEOMETRY_H
#define PAGEGUIDEGEOMETRY_H

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QRectF>

/**
 * Pure page-guide overlay geometry: 8 scale-handle anchors, hit-test, and
 * resize-from-handle math (Ctrl centre / Shift aspect). No ImageView state.
 *
 * Handle indices: 0=TL 1=T 2=TR 3=R 4=BR 5=B 6=BL 7=L
 */
namespace PageGuideGeometry {

/** Viewport hit radius for scale grips (matches paint size). */
constexpr qreal kScaleHitPx = 12.0;

/** Minimum page-guide edge length after resize (scene units). */
constexpr qreal kMinSide = 32.0;

/** Eight handle centres for an axis-aligned view rect (TL… clockwise). */
void handlePoints(const QRect &viewRect, QPointF out[8]);

/** Nearest handle under @p viewPos, or -1. */
int handleIndexAt(const QPoint &viewPos, const QRect &viewRect,
                  qreal hitPx = kScaleHitPx);

/**
 * Resize @p startRect from handle @p handle under pointer @p scenePos.
 * @p fromCenter (Ctrl) scales about centre; @p lockAspect (Shift) locks aspect
 * on corner handles. Enforces kMinSide.
 */
QRectF rectFromHandleDrag(const QPointF &scenePos, const QRectF &startRect, int handle,
                          bool fromCenter, bool lockAspect, qreal minSide = kMinSide);

} // namespace PageGuideGeometry

#endif // PAGEGUIDEGEOMETRY_H
