// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TEXTSEARCHPOLICY_H
#define TEXTSEARCHPOLICY_H

#include <QRectF>
#include <QString>
#include <QVector>

namespace TextSearchPolicy {

/** One highlighted hit: region index + horizontal fraction of the bbox (LTR approx). */
struct SearchHit {
    int regionIndex = -1;
    /** Inclusive start / exclusive end along the region width in [0, 1]. */
    qreal startFrac = 0.0;
    qreal endFrac = 1.0;
};

/** Lowercase + collapsed whitespace (OCR-light). */
QString normalizeForSearch(QString s);

/** Alphanumeric-only form for fuzzy OCR (ignore punctuation/spaces). */
QString alnumOnly(const QString &s);

/**
 * True if @p regionText matches pre-normalized @p queryNorm / @p queryAlnum.
 * Exact: normalized contains. Fuzzy: alnum contains + light edit distance.
 */
bool regionMatchesQuery(const QString &regionText, const QString &queryNorm,
                        const QString &queryAlnum, bool fuzzy);

/** Convenience: normalize @p query then match. */
bool matches(const QString &regionText, const QString &query, bool fuzzy);

/**
 * Reading-order indices for non-empty text regions (top-to-bottom, LTR rows).
 * @p bboxes parallel to region texts; empty bbox skips geometry and keeps index order.
 */
QVector<int> readingOrderIndices(const QVector<QString> &texts,
                                 const QVector<QRectF> &bboxes,
                                 qreal topTolerance = 4.0);

/**
 * Find search hits on a page of text regions.
 * - Exact matches may span consecutive reading-order regions (joined with a space).
 * - Fuzzy stays per-region (OCR slip); those hits highlight the full box.
 * - Exact hits store LTR width fractions for partial-box highlight.
 */
QVector<SearchHit> findHits(const QVector<QString> &texts,
                            const QVector<QRectF> &bboxes,
                            const QString &query,
                            bool fuzzy);

} // namespace TextSearchPolicy

#endif // TEXTSEARCHPOLICY_H
