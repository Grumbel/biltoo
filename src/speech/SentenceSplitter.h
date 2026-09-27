// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>
#include <QSet>
#include "Sentence.h"

// Heuristic sentence splitter -- mirrors piper_server/sentence_splitter.py.
// Keep the two in sync if you tune the abbreviation list or boundary rules;
// see that file's docstring for why this is a heuristic and not a full
// tokenizer (abbreviations, decimals, initials are handled; unusual
// abbreviations and nested quotes are known gaps), and for why newlines are
// always a hard boundary and very long punctuation-free runs get further
// subdivided at whitespace (max_len).
class SentenceSplitter {
public:
    static constexpr int kDefaultMaxLen = 280;

    // ids are assigned sequentially starting at startId, in document order.
    static QVector<Sentence> split(const QString &text, int startId = 0, int maxLen = kDefaultMaxLen);

private:
    static bool endsWithAbbreviation(const QString &text, int periodPos);
    static bool isDecimalPoint(const QString &text, int periodPos);
    static QString lastWord(const QString &text, int pos);
    // Subdivides [start, end) so no piece exceeds maxLen, breaking at
    // whitespace when available; hard-cuts only as a last resort.
    static QVector<QPair<int, int>> splitLongRange(const QString &text, int start, int end, int maxLen);
};

