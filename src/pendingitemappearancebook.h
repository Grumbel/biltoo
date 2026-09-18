// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PENDINGITEMAPPEARANCEBOOK_H
#define PENDINGITEMAPPEARANCEBOOK_H

#include "imageview_types.h"

#include <QHash>

class ImageItem;

/**
 * Content appearance staged by Duplicate until bindSelectedSessionIds.
 * Keys are live ImageItem pointers (GUI-only); cleared on session wipe.
 */
struct PendingItemAppearanceBook {
    QHash<ImageItem *, WorkspaceItemState> byItem;

    void clear() { byItem.clear(); }

    bool contains(ImageItem *item) const
    {
        return item && byItem.contains(item);
    }

    void insert(ImageItem *item, const WorkspaceItemState &state)
    {
        if (item) {
            byItem.insert(item, state);
        }
    }

    /** Take staged state for @p item; false if none. */
    bool take(ImageItem *item, WorkspaceItemState *out)
    {
        if (!item || !out) {
            return false;
        }
        const auto it = byItem.constFind(item);
        if (it == byItem.cend()) {
            return false;
        }
        *out = *it;
        byItem.erase(it);
        return true;
    }
};

#endif // PENDINGITEMAPPEARANCEBOOK_H
