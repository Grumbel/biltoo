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

} // namespace BatchTargets

#endif
