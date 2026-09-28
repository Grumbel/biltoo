// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTSELECTION_H
#define TEXTSELECTION_H

#include "imageview_types.h"

#include <QString>
#include <QStringList>
#include <QVector>

/**
 * One selected text region, optionally spanning many session images.
 * @p text is a snapshot at selection time so copy works after page change
 * without reloading every page layer.
 */
struct TextSelRef {
    SessionImageId sessionId = kInvalidSessionImageId;
    int regionIndex = -1;
    QString text;
};

/**
 * Multi-page text selection bag (data-driven; view projects current page).
 * Order is append / replace-per-session order (reading order within a page
 * is the caller's responsibility when building region indices).
 */
class TextSelection
{
public:
    bool isEmpty() const { return m_refs.isEmpty(); }
    int size() const { return m_refs.size(); }
    const QVector<TextSelRef> &refs() const { return m_refs; }

    void clear() { m_refs.clear(); }

    /** Drop all refs for @p sessionId, then append @p indices with texts. */
    void setForSession(SessionImageId sessionId,
                       const QVector<int> &regionIndices,
                       const QVector<QString> &texts)
    {
        if (sessionId == kInvalidSessionImageId) {
            return;
        }
        QVector<TextSelRef> kept;
        kept.reserve(m_refs.size());
        for (const TextSelRef &r : m_refs) {
            if (r.sessionId != sessionId) {
                kept.append(r);
            }
        }
        const int n = regionIndices.size();
        for (int i = 0; i < n; ++i) {
            TextSelRef r;
            r.sessionId = sessionId;
            r.regionIndex = regionIndices.at(i);
            if (i < texts.size()) {
                r.text = texts.at(i);
            }
            kept.append(r);
        }
        m_refs = std::move(kept);
    }

    QVector<int> regionIndicesFor(SessionImageId sessionId) const
    {
        QVector<int> out;
        if (sessionId == kInvalidSessionImageId) {
            return out;
        }
        for (const TextSelRef &r : m_refs) {
            if (r.sessionId == sessionId && r.regionIndex >= 0) {
                out.append(r.regionIndex);
            }
        }
        return out;
    }

    /** Snapshot texts in bag order (all pages). */
    QString joinedText() const
    {
        QStringList lines;
        for (const TextSelRef &r : m_refs) {
            if (!r.text.isEmpty()) {
                lines.append(r.text);
            }
        }
        return lines.join(QLatin1Char('\n'));
    }

private:
    QVector<TextSelRef> m_refs;
};

#endif // TEXTSELECTION_H
