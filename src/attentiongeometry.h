// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ATTENTIONGEOMETRY_H
#define ATTENTIONGEOMETRY_H

#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QVector>

/**
 * Pure attention-point geometry (normalized 0–1 content space and viewport hits).
 * No ImageView / session state.
 */
namespace AttentionGeometry {

/** Secondary (non-primary) handle hit radius in viewport pixels. */
constexpr qreal kHandleScreenPx = 12.0;
/** Primary (index 0) handle hit radius in viewport pixels. */
constexpr qreal kPrimaryScreenPx = 15.0;

/** Map normalized content point into @p contentRect local coordinates. */
QPointF localFromNorm(const QPointF &norm, const QRectF &contentRect);

/**
 * Nearest attention handle under @p viewPos among viewport positions @p viewPts.
 * Index 0 uses kPrimaryScreenPx; others kHandleScreenPx. Returns -1 if none.
 */
int handleIndexAt(const QPoint &viewPos, const QVector<QPointF> &viewPts);

} // namespace AttentionGeometry

#endif // ATTENTIONGEOMETRY_H
