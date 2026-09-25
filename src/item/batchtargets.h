// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef BATCHTARGETS_H
#define BATCHTARGETS_H

#include "imageview_types.h"

#include <QList>
#include <QString>

class ImageItem;
class ImageView;

/**
 * One batch-appearance target: durable identity always, live tile when present.
 * Non-live rows still receive ItemWorld writes (Gallery virtual slots / filmstrip).
 */
struct BatchAppearanceTarget {
    SessionImageId sessionId = kInvalidSessionImageId;
    QString path;
    ImageItem *live = nullptr; /**< nullptr → ItemWorld-only apply */
    int sessionIndex = -1;
};

namespace BatchTargets {

enum class Mode {
    Current = 0,     /**< Primary / current session image only */
    Selection = 1,   /**< Live scene selection ∪ filmstrip multi-select */
    IndexRange = 2,  /**< Inclusive session list indices [from, to] */
    EvenIndices = 3, /**< Session list indices 0, 2, 4, … (often recto) */
    OddIndices = 4,  /**< Session list indices 1, 3, 5, … (often verso) */
};

/**
 * Resolve targets for multi-apply. @p filmstripIds may be empty.
 * @p rangeFrom / @p rangeTo used only for IndexRange (clamped to session size).
 */
QList<BatchAppearanceTarget> resolve(ImageView *view,
                                     Mode mode,
                                     const QList<SessionImageId> &filmstripIds,
                                     int rangeFrom = 0,
                                     int rangeTo = -1);

/**
 * Session-list indices for IndexRange / EvenIndices / OddIndices.
 * Empty for Current / Selection (those need a live view).
 * rangeTo < 0 means last index (sessionSize - 1).
 */
inline QList<int> sessionIndices(Mode mode, int sessionSize,
                                 int rangeFrom = 0, int rangeTo = -1)
{
    QList<int> out;
    if (sessionSize < 1) {
        return out;
    }
    if (mode == Mode::IndexRange) {
        int from = rangeFrom < 0 ? 0 : rangeFrom;
        if (from > sessionSize - 1) {
            from = sessionSize - 1;
        }
        int to = rangeTo < 0 ? (sessionSize - 1) : rangeTo;
        if (to > sessionSize - 1) {
            to = sessionSize - 1;
        }
        if (to < 0) {
            to = 0;
        }
        if (to < from) {
            const int tmp = from;
            from = to;
            to = tmp;
        }
        out.reserve(to - from + 1);
        for (int i = from; i <= to; ++i) {
            out.append(i);
        }
        return out;
    }
    if (mode == Mode::EvenIndices || mode == Mode::OddIndices) {
        const int parity = (mode == Mode::EvenIndices) ? 0 : 1;
        for (int i = 0; i < sessionSize; ++i) {
            if ((i % 2) == parity) {
                out.append(i);
            }
        }
        return out;
    }
    return out;
}

} // namespace BatchTargets

#endif
