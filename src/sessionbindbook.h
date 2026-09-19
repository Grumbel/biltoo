// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONBINDBOOK_H
#define SESSIONBINDBOOK_H

#include "imageview_types.h"

#include <QHash>
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
 * Session-scoped LoadAdd bind queue and related select/index maps.
 *
 * Decode orchestration and canvas placement stay on ImageView; this bag owns
 * the pending lists that clear together on session wipe.
 */
struct SessionBindBook {
    QList<PendingSessionBind> binds;
    /** Session list index to assign when a LoadAdd for path finishes. */
    QHash<QString, int> indexByPath;
    /** Select these session ids when LoadAdd creates their tiles (paste). */
    QSet<SessionImageId> selectIds;

    void clear()
    {
        binds.clear();
        indexByPath.clear();
        selectIds.clear();
    }

    /** Take the front pending bind; false if empty. */
    bool takeFront(PendingSessionBind *out)
    {
        if (binds.isEmpty() || !out) {
            return false;
        }
        *out = binds.takeFirst();
        return true;
    }

    bool hasBindForPath(const QString &path) const
    {
        if (path.isEmpty()) {
            return false;
        }
        for (const PendingSessionBind &b : binds) {
            if (b.path == path) {
                return true;
            }
        }
        return false;
    }

    int countBindsForPath(const QString &path) const
    {
        int n = 0;
        for (const PendingSessionBind &b : binds) {
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
        for (int bi = 0; bi < binds.size(); ++bi) {
            if (binds.at(bi).path != path) {
                continue;
            }
            *out = binds.takeAt(bi);
            return true;
        }
        return false;
    }

    void removeBindsForSessionId(SessionImageId sessionId)
    {
        if (sessionId == kInvalidSessionImageId) {
            return;
        }
        for (int i = binds.size() - 1; i >= 0; --i) {
            if (binds.at(i).id == sessionId) {
                binds.removeAt(i);
            }
        }
    }

    void append(const PendingSessionBind &b) { binds.append(b); }
};

#endif // SESSIONBINDBOOK_H
