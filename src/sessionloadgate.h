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

    // --- Deferred scene positions ----------------------------------------------

    QHash<QString, QPointF> &pendingScenePos() { return m_pendingScenePos; }
    const QHash<QString, QPointF> &pendingScenePos() const
    {
        return m_pendingScenePos;
    }

    void clearPendingScenePos() { m_pendingScenePos.clear(); }

private:
    LoadGeneration m_gen;
    QHash<QString, int> m_pendingWorkspacePaths;
    QList<WorkspaceItemState> m_pendingRestoreStates;
    QHash<QString, QPointF> m_pendingScenePos;
};

#endif // SESSIONLOADGATE_H
