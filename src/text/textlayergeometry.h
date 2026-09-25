// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERGEOMETRY_H
#define TEXTLAYERGEOMETRY_H

#include <QRectF>
#include <QVector>

/**
 * Pure text-region selection geometry (image-space rects).
 * Callers map page regions → image rects; this bag only intersects and orders.
 */
namespace TextLayerGeometry {

/**
 * Indices of @p regionRects that intersect @p rubber (empty rects skipped).
 * Does not sort.
 */
QVector<int> indicesIntersecting(const QVector<QRectF> &regionRects, const QRectF &rubber);

/**
 * Stable reading order for search / selection.
 * When @p blockIds is the same size as @p regionRects and both sides have
 * blockId >= 0, sort primarily by block (MuPDF paragraph/column island), then
 * top-to-bottom / LTR within the block. Otherwise fall back to page-wide
 * top-then-left (legacy).
 * @p indices are rearranged in place; each index must be valid for @p regionRects.
 */
void sortReadingOrder(QVector<int> *indices, const QVector<QRectF> &regionRects,
                      qreal topTolerance = 4.0,
                      const QVector<int> *blockIds = nullptr);

} // namespace TextLayerGeometry

#endif // TEXTLAYERGEOMETRY_H
