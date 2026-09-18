// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PATHITEMSTATEBOOK_H
#define PATHITEMSTATEBOOK_H

#include "imageview_types.h"

#include <QHash>
#include <QString>

/**
 * Path-keyed WorkspaceItemState map.
 *
 * Bound session images: content appearance is SessionAppearanceStore only.
 * This bag remains the Workspace free-placement cache and unbound-tile
 * fallback (IDENTITY: path is not the content key when an id is bound).
 */
struct PathItemStateBook {
    QHash<QString, WorkspaceItemState> byPath;

    void clear() { byPath.clear(); }

    bool contains(const QString &path) const
    {
        return !path.isEmpty() && byPath.contains(path);
    }

    const WorkspaceItemState *get(const QString &path) const
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = byPath.constFind(path);
        if (it == byPath.cend()) {
            return nullptr;
        }
        return &(*it);
    }

    void set(const QString &path, const WorkspaceItemState &state)
    {
        if (!path.isEmpty()) {
            byPath.insert(path, state);
        }
    }

    void remove(const QString &path) { byPath.remove(path); }
};

#endif // PATHITEMSTATEBOOK_H
