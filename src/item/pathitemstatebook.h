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
 * Bound session images: content+pose are ItemWorld sparse + XDG only.
 * ItemWorld::setPathState is a no-op when sessionId is bound (IDENTITY: path is
 * not the identity key). This bag holds unbound-tile content+placement only.
 */
class PathItemStateBook
{
public:
    void clear() { m_byPath.clear(); }

    bool contains(const QString &path) const
    {
        return !path.isEmpty() && m_byPath.contains(path);
    }

    const WorkspaceItemState *get(const QString &path) const
    {
        if (path.isEmpty()) {
            return nullptr;
        }
        const auto it = m_byPath.constFind(path);
        if (it == m_byPath.cend()) {
            return nullptr;
        }
        return &(*it);
    }

    void set(const QString &path, const WorkspaceItemState &state)
    {
        if (!path.isEmpty()) {
            m_byPath.insert(path, state);
        }
    }

    void remove(const QString &path) { m_byPath.remove(path); }

    /** Take state for @p path; false if none. */
    bool take(const QString &path, WorkspaceItemState *out)
    {
        if (path.isEmpty() || !out) {
            return false;
        }
        const auto it = m_byPath.find(path);
        if (it == m_byPath.end()) {
            return false;
        }
        *out = *it;
        m_byPath.erase(it);
        return true;
    }

    int size() const { return m_byPath.size(); }
    bool isEmpty() const { return m_byPath.isEmpty(); }

private:
    QHash<QString, WorkspaceItemState> m_byPath;
};

#endif // PATHITEMSTATEBOOK_H
