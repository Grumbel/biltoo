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

/** Map content-local point into normalized 0–1 (clamped). Empty rect → (0,0). */
QPointF normFromLocal(const QPointF &local, const QRectF &contentRect);

/** Clamp a single normalized point into [0, 1]². */
QPointF clampNorm(const QPointF &norm);

/** Clamp every point in @p pts into [0, 1]². */
QVector<QPointF> clampNormPoints(const QVector<QPointF> &pts);

/**
 * Convert a content-local delta into normalized delta (divide by content size).
 * Empty rect → (0, 0).
 */
QPointF normDeltaFromLocalDelta(const QPointF &localDelta, const QRectF &contentRect);

/**
 * Translate selected indices of @p startPts by @p dNorm and clamp each to [0, 1]².
 */
QVector<QPointF> translateSelectedNorms(const QVector<QPointF> &startPts,
                                        const QVector<int> &selected,
                                        const QPointF &dNorm);

/**
 * Nearest attention handle under @p viewPos among viewport positions @p viewPts.
 * Index 0 uses kPrimaryScreenPx; others kHandleScreenPx. Returns -1 if none.
 */
int handleIndexAt(const QPoint &viewPos, const QVector<QPointF> &viewPts);

} // namespace AttentionGeometry

#endif // ATTENTIONGEOMETRY_H
