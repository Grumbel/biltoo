// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textsearchpolicy.h"

namespace TextSearchPolicy {

QString normalizeForSearch(QString s)
{
    s = s.toLower();
    // Collapse whitespace; keep letters/digits for light OCR tolerance.
    QString out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (QChar c : s) {
        if (c.isSpace()) {
            if (!prevSpace && !out.isEmpty()) {
                out.append(QLatin1Char(' '));
                prevSpace = true;
            }
            continue;
        }
        prevSpace = false;
        out.append(c);
    }
    return out.trimmed();
}

/** Alphanumeric-only form for fuzzy OCR (ignore punctuation/spaces). */
QString alnumOnly(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isLetterOrNumber()) {
            out.append(c.toLower());
        }
    }
    return out;
}

bool regionMatchesQuery(const QString &regionText, const QString &queryNorm,
                        const QString &queryAlnum, bool fuzzy)
{
    if (queryNorm.isEmpty()) {
        return false;
    }
    const QString rn = normalizeForSearch(regionText);
    if (rn.contains(queryNorm)) {
        return true;
    }
    if (!fuzzy) {
        return false;
    }
    // Alnum-only contains (helps OCR noise / missing spaces / punctuation).
    if (!queryAlnum.isEmpty()) {
        const QString ra = alnumOnly(regionText);
        if (ra.contains(queryAlnum)) {
            return true;
        }
        // Light edit distance: allow one substitution/insert/delete for queries
        // long enough that a single OCR slip is plausible (not for 1–2 chars).
        if (queryAlnum.size() >= 4 && ra.size() >= queryAlnum.size() - 1) {
            const int qn = queryAlnum.size();
            for (int i = 0; i + qn - 1 <= ra.size(); ++i) {
                const int window = qMin(qn + 1, ra.size() - i);
                for (int w = qMax(qn - 1, 1); w <= window; ++w) {
                    const QString slice = ra.mid(i, w);
                    // Hamming-ish: count mismatches with simple DP bound.
                    int dist = 0;
                    const int a = slice.size();
                    const int b = qn;
                    // Bounded Levenshtein early-out (max dist 1).
                    if (qAbs(a - b) > 1) {
                        continue;
                    }
                    if (a == b) {
                        for (int k = 0; k < a; ++k) {
                            if (slice.at(k) != queryAlnum.at(k)) {
                                ++dist;
                                if (dist > 1) {
                                    break;
                                }
                            }
                        }
                        if (dist <= 1) {
                            return true;
                        }
                    } else {
                        // Length differs by 1: accept if one is subsequence of other.
                        const QString &shorter = a < b ? slice : queryAlnum;
                        const QString &longer = a < b ? queryAlnum : slice;
                        int si = 0;
                        for (int li = 0; li < longer.size() && si < shorter.size(); ++li) {
                            if (longer.at(li) == shorter.at(si)) {
                                ++si;
                            }
                        }
                        if (si == shorter.size()) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}


bool matches(const QString &regionText, const QString &query, bool fuzzy)
{
    const QString qn = normalizeForSearch(query);
    const QString qa = alnumOnly(query);
    return regionMatchesQuery(regionText, qn, qa, fuzzy);
}

} // namespace TextSearchPolicy
