// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONBINDBOOK_H
#define SESSIONBINDBOOK_H

#include "imageview_types.h"
#include "item/pendingitemappearancebook.h"

#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>

/**
 * One in-flight LoadAdd bind: session slot + optional drop scene position.
 * ImageView still places the tile and applies appearance.
 */
struct PendingSessionBind {
    QString path;
    SessionImageId id = kInvalidSessionImageId;
    int index = -1;
    QPointF scenePos;
    bool hasScenePos = false;
};

/**
 * Session-scoped LoadAdd bind queue and select-on-create ids.
 *
 * Decode orchestration and canvas placement stay on ImageView; this bag owns
 * the pending lists and collision-recovery appearance staging that clear
 * together on session wipe.
 */
class SessionBindBook {
public:
    void clear()
    {
        m_binds.clear();
        m_selectIds.clear();
        m_pendingAppearance.clear();
    }

    bool hasBindForPath(const QString &path) const
    {
        if (path.isEmpty()) {
            return false;
        }
        for (const PendingSessionBind &b : m_binds) {
            if (b.path == path) {
                return true;
            }
        }
        return false;
    }

    int countBindsForPath(const QString &path) const
    {
        int n = 0;
        for (const PendingSessionBind &b : m_binds) {
            if (b.path == path) {
                ++n;
            }
        }
        return n;
    }

    /** Remove first bind for @p path into @p out. */
    bool takeBind(const QString &path, PendingSessionBind *out)
    {
        if (!out || path.isEmpty()) {
            return false;
        }
        for (int bi = 0; bi < m_binds.size(); ++bi) {
            if (m_binds.at(bi).path != path) {
                continue;
            }
            *out = m_binds.takeAt(bi);
            return true;
        }
        return false;
    }

    void removeBindsForSessionId(SessionImageId sessionId)
    {
        if (sessionId == kInvalidSessionImageId) {
            return;
        }
        for (int i = m_binds.size() - 1; i >= 0; --i) {
            if (m_binds.at(i).id == sessionId) {
                m_binds.removeAt(i);
            }
        }
    }

    bool isEmpty() const { return m_binds.isEmpty(); }

    int bindCount() const { return m_binds.size(); }

    void clearSelectIds() { m_selectIds.clear(); }

    void addSelectId(SessionImageId id)
    {
        if (id != kInvalidSessionImageId) {
            m_selectIds.insert(id);
        }
    }

    /** @return true when @p id was pending selection. */
    bool removeSelectId(SessionImageId id) { return m_selectIds.remove(id); }

    void append(const PendingSessionBind &b) { m_binds.append(b); }

    const PendingSessionBind &bindAt(int i) const { return m_binds.at(i); }

    void removeBindAt(int i) { m_binds.removeAt(i); }

    bool takeBindAt(int i, PendingSessionBind *out)
    {
        if (!out || i < 0 || i >= m_binds.size()) {
            return false;
        }
        *out = m_binds.takeAt(i);
        return true;
    }

    /** Collision-recovery content appearance staged until bind-on-create. */
    PendingItemAppearanceBook &pendingAppearance() { return m_pendingAppearance; }
    const PendingItemAppearanceBook &pendingAppearance() const { return m_pendingAppearance; }

private:
    QList<PendingSessionBind> m_binds;
    QSet<SessionImageId> m_selectIds;
    PendingItemAppearanceBook m_pendingAppearance;
};

#endif // SESSIONBINDBOOK_H
