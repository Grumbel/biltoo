// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PENDINGITEMAPPEARANCEBOOK_H
#define PENDINGITEMAPPEARANCEBOOK_H

#include "imageview_types.h"

#include <QHash>

class ImageItem;

/**
 * Content appearance staged when a tile cannot bind its SessionImageId on
 * create (id already owned by another live tile — corruption / race).
 * Normal Duplicate binds on create (biltoo-2109); this book is collision
 * recovery only (Duplicate / LoadAdd bind-on-create consumes staged rows).
 * Keys are live ImageItem pointers (GUI-only); cleared on session wipe.
 */
class PendingItemAppearanceBook
{
public:
    void clear() { m_byItem.clear(); }

    bool contains(ImageItem *item) const
    {
        return item && m_byItem.contains(item);
    }

    void insert(ImageItem *item, const WorkspaceItemState &state)
    {
        if (item) {
            m_byItem.insert(item, state);
        }
    }

    /** Take staged state for @p item; false if none. */
    bool take(ImageItem *item, WorkspaceItemState *out)
    {
        if (!item || !out) {
            return false;
        }
        const auto it = m_byItem.find(item);
        if (it == m_byItem.end()) {
            return false;
        }
        *out = *it;
        m_byItem.erase(it);
        return true;
    }

    int size() const { return m_byItem.size(); }
    bool isEmpty() const { return m_byItem.isEmpty(); }

private:
    QHash<ImageItem *, WorkspaceItemState> m_byItem;
};

#endif // PENDINGITEMAPPEARANCEBOOK_H
