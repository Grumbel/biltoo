// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SentenceSplitter.h"

#include <QPair>
#include <QRegularExpression>
#include <algorithm>

namespace {

const QSet<QString> &abbreviations()
{
    static const QSet<QString> kAbbrevs = {
        "mr", "mrs", "ms", "dr", "prof", "sr", "jr", "st", "vs", "etc",
        "e.g", "i.e", "fig", "vol", "no", "approx", "gen", "col", "lt",
        "capt", "cmdr", "gov", "rep", "sen", "rev", "inc", "ltd", "co",
    };
    return kAbbrevs;
}

// Matches: terminal punctuation, optional closing quote/bracket, whitespace,
// followed by (lookahead) a capital letter / opening quote / end of string.
const QRegularExpression &boundaryRegex()
{
    static const QRegularExpression kRe(
        QStringLiteral("[.!?]+[\"'\\x{201d}\\x{2019})\\]]*\\s+(?=[A-Z\"'\\x{201c}\\x{2018}(\\[]|$)"));
    return kRe;
}

// Newlines are always a hard boundary regardless of punctuation -- see the
// header/sentence_splitter.py docstring for why (list items, poetry,
// dialogue without terminal punctuation used to all merge into one segment).
const QRegularExpression &newlineRegex()
{
    static const QRegularExpression kRe(QStringLiteral("\\n+"));
    return kRe;
}

} // namespace

QString SentenceSplitter::lastWord(const QString &text, int pos)
{
    int i = pos;
    while (i > 0 && (text[i - 1].isLetterOrNumber() || text[i - 1] == QLatin1Char('.'))) {
        --i;
    }
    return text.mid(i, pos - i);
}

bool SentenceSplitter::endsWithAbbreviation(const QString &text, int periodPos)
{
    QString word = lastWord(text, periodPos);
    QString bare = word;
    while (bare.endsWith(QLatin1Char('.'))) {
        bare.chop(1);
    }
    if (bare.isEmpty()) {
        return false;
    }
    if (abbreviations().contains(bare.toLower())) {
        return true;
    }
    if (bare.size() == 1 && bare[0].isUpper()) {
        return true; // single-letter initial, e.g. "J. R. R. Tolkien"
    }
    return false;
}

bool SentenceSplitter::isDecimalPoint(const QString &text, int periodPos)
{
    // periodPos points at the first char of the punctuation run; check
    // for digit-dot-digit straddling it.
    if (periodPos <= 0 || periodPos + 1 >= text.size()) {
        return false;
    }
    return text[periodPos - 1].isDigit() && text[periodPos] == QLatin1Char('.')
        && text[periodPos + 1].isDigit();
}

QVector<QPair<int, int>> SentenceSplitter::splitLongRange(const QString &text, int start, int end, int maxLen)
{
    QVector<QPair<int, int>> pieces;
    if (maxLen <= 0 || end - start <= maxLen) {
        pieces.append({start, end});
        return pieces;
    }

    int pos = start;
    while (end - pos > maxLen) {
        int searchLimit = pos + maxLen;
        int breakAt = -1;
        // Look for the last whitespace at or before the limit, but after pos.
        for (int i = searchLimit; i > pos; --i) {
            if (text[i - 1].isSpace()) {
                breakAt = i - 1;
                break;
            }
        }
        if (breakAt <= pos) {
            breakAt = searchLimit; // no whitespace to break on; hard cut
        }
        pieces.append({pos, breakAt});
        pos = breakAt;
    }
    pieces.append({pos, end});
    return pieces;
}

QVector<Sentence> SentenceSplitter::split(const QString &text, int startId, int maxLen)
{
    QVector<Sentence> result;
    if (text.isEmpty()) {
        return result;
    }

    QSet<int> boundarySet;

    auto punctIt = boundaryRegex().globalMatch(text);
    while (punctIt.hasNext()) {
        auto m = punctIt.next();
        int punctStart = m.capturedStart(0);
        if (endsWithAbbreviation(text, punctStart) || isDecimalPoint(text, punctStart)) {
            continue;
        }
        boundarySet.insert(m.capturedEnd(0));
    }

    auto nlIt = newlineRegex().globalMatch(text);
    while (nlIt.hasNext()) {
        boundarySet.insert(nlIt.next().capturedEnd(0));
    }

    QVector<int> boundaries;
    for (int b : boundarySet) {
        if (b > 0 && b < text.size()) {
            boundaries.append(b);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());

    // Raw (untrimmed) ranges covering the whole text contiguously.
    QVector<QPair<int, int>> rawRanges;
    int start = 0;
    for (int end : boundaries) {
        rawRanges.append({start, end});
        start = end;
    }
    rawRanges.append({start, static_cast<int>(text.size())});

    int nextId = startId;
    auto emitChunk = [&](int chunkStart, int chunkEnd) {
        QString chunk = text.mid(chunkStart, chunkEnd - chunkStart);
        QString trimmed = chunk.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        bool hasSpeakableContent = std::any_of(trimmed.begin(), trimmed.end(),
                                                [](QChar ch) { return ch.isLetterOrNumber(); });
        if (!hasSpeakableContent) {
            // No letters or digits at all -- e.g. stray punctuation or a
            // leftover HTML entity/symbol from imperfect HTML-to-text
            // extraction ("---", "»"). Piper's own text normalization
            // reduces these to zero phonemes and then crashes trying to
            // write a WAV header for zero frames, so skip sending them at
            // all rather than relying on the server to degrade gracefully.
            return;
        }
        int leadingWs = chunk.indexOf(trimmed.left(1));
        int offset = chunkStart + (leadingWs < 0 ? 0 : leadingWs);
        result.append(Sentence{nextId++, offset, offset + static_cast<int>(trimmed.size()), trimmed});
    };

    for (const auto &range : rawRanges) {
        for (const auto &sub : splitLongRange(text, range.first, range.second, maxLen)) {
            emitChunk(sub.first, sub.second);
        }
    }

    return result;
}
