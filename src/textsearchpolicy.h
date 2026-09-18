// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTSEARCHPOLICY_H
#define TEXTSEARCHPOLICY_H

#include <QString>

/**
 * Pure page-text Find matching (OCR-tolerant normalize + optional fuzzy).
 * No ImageView / Thumtoo dependency.
 */
namespace TextSearchPolicy {

QString normalizeForSearch(QString s);
QString alnumOnly(const QString &s);

/**
 * @p queryNorm / @p queryAlnum from normalizeForSearch / alnumOnly on the query.
 * Fuzzy allows alnum contains and distance-1 slips for queries length ≥ 4.
 */
bool regionMatchesQuery(const QString &regionText, const QString &queryNorm,
                        const QString &queryAlnum, bool fuzzy);

/** Convenience: normalize @p query then match. */
bool matches(const QString &regionText, const QString &query, bool fuzzy);

} // namespace TextSearchPolicy

#endif // TEXTSEARCHPOLICY_H
