// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// A sentence located within the *rendered* plain text of the QTextDocument
// (i.e. offsets are computed after HTML has already been parsed by Qt --
// not offsets into the original HTML source). This is what lets
// DocumentView::highlightSentence() build a QTextCursor directly.
struct Sentence {
    int id = -1;      // matches the id sent to the Piper server for this sentence
    int start = 0;    // inclusive offset into QTextDocument::toPlainText()
    int end = 0;      // exclusive
    QString text;
};
