// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONLOADGATE_H
#define SESSIONLOADGATE_H

#include "imageview_types.h"
#include "loadgeneration.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>

/**
 * Session-scoped async load bookkeeping for ImageView.
 *
 * Owns the LoadGeneration token plus pending LoadAdd / LoadRestore maps that
 * clear together on session wipe. Decode orchestration stays on ImageView;
 * this type is the gate workers consult and Open/leave bumps.
 */
class SessionLoadGate
{
public:
    LoadGeneration::Value generation() const { return m_gen.current(); }
    bool accepts(LoadGeneration::Value captured) const
    {
        return m_gen.accepts(captured);
    }

    /** Bump generation only (in-flight jobs reject; pending maps unchanged). */
    LoadGeneration::Value bumpGeneration() { return m_gen.bump(); }

    /**
     * Drop pending path counts, restore states, and scene positions.
     * Does not bump generation — pair with bumpGeneration when cancelling work.
     */
    void clearPending()
    {
        m_pendingWorkspacePaths.clear();
        m_pendingRestoreStates.clear();
        m_pendingScenePos.clear();
    }

    /** Session Open / full invalidate: bump + clear pending maps. */
    LoadGeneration::Value invalidatePending()
    {
        clearPending();
        return m_gen.bump();
    }

    // --- LoadAdd path refcounts ------------------------------------------------

    bool hasPendingWorkspacePaths() const
    {
        return !m_pendingWorkspacePaths.isEmpty();
    }

    /** Sum of LoadAdd path refcounts (status / HUD). */
    int pendingWorkspaceAddCount() const
    {
        int n = 0;
        for (int c : m_pendingWorkspacePaths) {
            n += c;
        }
        return n;
    }

    void clearPendingWorkspacePaths() { m_pendingWorkspacePaths.clear(); }

    void addPendingWorkspacePath(const QString &path)
    {
        if (!path.isEmpty()) {
            m_pendingWorkspacePaths[path] += 1;
        }
    }

    bool containsPendingWorkspacePath(const QString &path) const
    {
        return m_pendingWorkspacePaths.contains(path);
    }

    /**
     * Decrement path refcount; remove when zero.
     * @return true if the path was present.
     */
    void removePendingWorkspacePath(const QString &path)
    {
        m_pendingWorkspacePaths.remove(path);
    }

    bool takePendingWorkspacePath(const QString &path)
    {
        const auto it = m_pendingWorkspacePaths.find(path);
        if (it == m_pendingWorkspacePaths.end()) {
            return false;
        }
        if (it.value() <= 1) {
            m_pendingWorkspacePaths.erase(it);
        } else {
            --(it.value());
        }
        return true;
    }

    // --- LoadRestore states ----------------------------------------------------

    QList<WorkspaceItemState> &pendingRestoreStates()
    {
        return m_pendingRestoreStates;
    }
    const QList<WorkspaceItemState> &pendingRestoreStates() const
    {
        return m_pendingRestoreStates;
    }

    int pendingRestoreCount() const { return m_pendingRestoreStates.size(); }

    /** Take first pending restore whose path matches @p path. */
    bool takePendingRestoreForPath(const QString &path, WorkspaceItemState *out)
    {
        if (!out || path.isEmpty()) {
            return false;
        }
        for (int i = 0; i < m_pendingRestoreStates.size(); ++i) {
            if (m_pendingRestoreStates.at(i).path != path) {
                continue;
            }
            *out = m_pendingRestoreStates.takeAt(i);
            return true;
        }
        return false;
    }

    // --- Deferred scene positions ----------------------------------------------

    QHash<QString, QPointF> &pendingScenePos() { return m_pendingScenePos; }
    const QHash<QString, QPointF> &pendingScenePos() const
    {
        return m_pendingScenePos;
    }

    void clearPendingScenePos() { m_pendingScenePos.clear(); }

    void setPendingScenePos(const QString &path, const QPointF &pos)
    {
        if (!path.isEmpty()) {
            m_pendingScenePos.insert(path, pos);
        }
    }

    void removePendingScenePos(const QString &path) { m_pendingScenePos.remove(path); }

    bool hasPendingScenePos(const QString &path) const
    {
        return !path.isEmpty() && m_pendingScenePos.contains(path);
    }

    /** Take deferred scene pos for @p path; false if none. */
    bool takePendingScenePos(const QString &path, QPointF *out)
    {
        if (path.isEmpty()) {
            return false;
        }
        const auto it = m_pendingScenePos.find(path);
        if (it == m_pendingScenePos.end()) {
            return false;
        }
        if (out) {
            *out = it.value();
        }
        m_pendingScenePos.erase(it);
        return true;
    }

private:
    LoadGeneration m_gen;
    QHash<QString, int> m_pendingWorkspacePaths;
    QList<WorkspaceItemState> m_pendingRestoreStates;
    QHash<QString, QPointF> m_pendingScenePos;
};

#endif // SESSIONLOADGATE_H
