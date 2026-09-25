// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "text/textsearchpolicy.h"
#include "text/textlayergeometry.h"

#include <QtGlobal>
#include <algorithm>
#include <QSet>

namespace TextSearchPolicy {

QString normalizeForSearch(QString s)
{
    s = s.toLower();
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
    if (!queryAlnum.isEmpty()) {
        const QString ra = alnumOnly(regionText);
        if (ra.contains(queryAlnum)) {
            return true;
        }
        if (queryAlnum.size() >= 4 && ra.size() >= queryAlnum.size() - 1) {
            const int qn = queryAlnum.size();
            for (int i = 0; i + qn - 1 <= ra.size(); ++i) {
                const int window = qMin(qn + 1, ra.size() - i);
                for (int w = qMax(qn - 1, 1); w <= window; ++w) {
                    const QString slice = ra.mid(i, w);
                    int dist = 0;
                    const int a = slice.size();
                    const int b = qn;
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

QVector<int> readingOrderIndices(const QVector<QString> &texts,
                                 const QVector<QRectF> &bboxes,
                                 qreal topTolerance)
{
    QVector<int> order;
    order.reserve(texts.size());
    for (int i = 0; i < texts.size(); ++i) {
        if (texts.at(i).trimmed().isEmpty()) {
            continue;
        }
        order.push_back(i);
    }
    if (bboxes.size() == texts.size() && !order.isEmpty()) {
        TextLayerGeometry::sortReadingOrder(&order, bboxes, topTolerance);
    }
    return order;
}

namespace {

struct StreamSpan {
    int regionIndex = -1;
    int streamStart = 0;
    int streamEnd = 0; // exclusive
};

QVector<SearchHit> hitsFromStreamMatch(const QVector<StreamSpan> &spans,
                                       int matchStart, int matchEnd)
{
    QVector<SearchHit> hits;
    if (matchStart < 0 || matchEnd <= matchStart) {
        return hits;
    }
    for (const StreamSpan &sp : spans) {
        if (sp.streamEnd <= matchStart || sp.streamStart >= matchEnd) {
            continue;
        }
        const int len = sp.streamEnd - sp.streamStart;
        if (len < 1) {
            continue;
        }
        const int localStart = qMax(0, matchStart - sp.streamStart);
        const int localEnd = qMin(len, matchEnd - sp.streamStart);
        SearchHit h;
        h.regionIndex = sp.regionIndex;
        h.startFrac = qreal(localStart) / qreal(len);
        h.endFrac = qreal(localEnd) / qreal(len);
        if (h.endFrac <= h.startFrac) {
            h.endFrac = qMin(1.0, h.startFrac + 0.05);
        }
        h.startFrac = qBound(0.0, h.startFrac, 1.0);
        h.endFrac = qBound(0.0, h.endFrac, 1.0);
        hits.push_back(h);
    }
    return hits;
}

} // namespace

QVector<SearchHit> findHits(const QVector<QString> &texts,
                            const QVector<QRectF> &bboxes,
                            const QString &query,
                            bool fuzzy)
{
    QVector<SearchHit> out;
    const QString qn = normalizeForSearch(query);
    if (qn.isEmpty() || texts.isEmpty()) {
        return out;
    }
    const QString qa = alnumOnly(query);
    const QVector<int> order = readingOrderIndices(texts, bboxes);

    // Build normalized reading-order stream.
    // Same-line, nearly adjacent boxes: join with no separator (PDF mid-word splits).
    // Otherwise insert a single space (word boundary between runs).
    QString stream;
    QVector<StreamSpan> spans;
    spans.reserve(order.size());
    int prevOrderPos = -1;
    for (int oi = 0; oi < order.size(); ++oi) {
        const int ri = order.at(oi);
        if (ri < 0 || ri >= texts.size()) {
            continue;
        }
        const QString rn = normalizeForSearch(texts.at(ri));
        if (rn.isEmpty()) {
            continue;
        }
        if (!stream.isEmpty()) {
            bool tightJoin = false;
            if (prevOrderPos >= 0 && bboxes.size() == texts.size()) {
                const int prevRi = order.at(prevOrderPos);
                if (prevRi >= 0 && prevRi < bboxes.size() && ri < bboxes.size()) {
                    const QRectF &a = bboxes.at(prevRi);
                    const QRectF &b = bboxes.at(ri);
                    if (a.isValid() && b.isValid()) {
                        const bool sameLine = qAbs(a.center().y() - b.center().y())
                            <= qMax(4.0, 0.6 * qMax(a.height(), b.height()));
                        const qreal gap = b.left() - a.right();
                        const qreal charW = a.width() / qMax(1, normalizeForSearch(texts.at(prevRi)).size());
                        // Gap smaller than ~1.25 em → likely same word / tight run.
                        // Only glue when boxes almost touch (PDF mid-glyph splits).
                        tightJoin = sameLine && gap < qMax(1.5, 0.35 * charW);
                    }
                }
            }
            if (!tightJoin) {
                stream.append(QLatin1Char(' '));
            }
        }
        StreamSpan sp;
        sp.regionIndex = ri;
        sp.streamStart = stream.size();
        stream.append(rn);
        sp.streamEnd = stream.size();
        spans.push_back(sp);
        prevOrderPos = oi;
    }

    // Exact: all occurrences on the joined stream (covers single- and multi-box).
    if (!stream.isEmpty()) {
        int from = 0;
        while (from < stream.size()) {
            const int at = stream.indexOf(qn, from);
            if (at < 0) {
                break;
            }
            const QVector<SearchHit> piece =
                hitsFromStreamMatch(spans, at, at + qn.size());
            for (const SearchHit &h : piece) {
                out.push_back(h);
            }
            from = at + qMax(1, qn.size());
        }
    }

    // Fuzzy per-region only when exact did not already cover that region for
    // this query (full-box highlight; no reliable char map).
    if (fuzzy) {
        QSet<int> exactRegions;
        for (const SearchHit &h : out) {
            exactRegions.insert(h.regionIndex);
        }
        for (int i = 0; i < texts.size(); ++i) {
            if (exactRegions.contains(i)) {
                continue;
            }
            if (regionMatchesQuery(texts.at(i), qn, qa, true)
                && !normalizeForSearch(texts.at(i)).contains(qn)) {
                SearchHit h;
                h.regionIndex = i;
                h.startFrac = 0.0;
                h.endFrac = 1.0;
                out.push_back(h);
            }
        }
    }

    // Stable order by region index then startFrac.
    std::sort(out.begin(), out.end(), [](const SearchHit &a, const SearchHit &b) {
        if (a.regionIndex != b.regionIndex) {
            return a.regionIndex < b.regionIndex;
        }
        return a.startFrac < b.startFrac;
    });
    return out;
}

} // namespace TextSearchPolicy
