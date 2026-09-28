// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTLAYERGEOMETRY_H
#define TEXTLAYERGEOMETRY_H

#include <QRectF>
#include <QVector>

/**
 * Pure text-region selection geometry.
 * Callers map page regions → image rects when needed; this bag intersects and orders.
 */
namespace TextLayerGeometry {

/**
 * Indices of @p regionRects that intersect @p rubber (empty rects skipped).
 * Does not sort.
 */
QVector<int> indicesIntersecting(const QVector<QRectF> &regionRects, const QRectF &rubber);

/**
 * Stable reading order for search / selection / TTS.
 *
 * When @p preferSourceOrder is true (OCR layers): keep ascending region index
 * — Tesseract ResultIterator order is already reading order; geometric re-sort
 * fights multi-column and Y-up page space.
 *
 * Otherwise: optional @p blockIds primary key, then visual top-to-bottom / LTR.
 * @p pageYUp true (DjVu page space): visual top is the larger Y
 * (QRectF::bottom after normalize); sorting by top() ascending would read the
 * page bottom-first. PDF/EPUB (MuPDF) are pageYUp false.
 */
void sortReadingOrder(QVector<int> *indices, const QVector<QRectF> &regionRects,
                      qreal topTolerance = 4.0,
                      const QVector<int> *blockIds = nullptr,
                      bool pageYUp = false,
                      bool preferSourceOrder = false);

} // namespace TextLayerGeometry

#endif // TEXTLAYERGEOMETRY_H
