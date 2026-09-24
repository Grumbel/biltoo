// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace path membership helpers used by setWorkspacePaths / bind.
// ImageView keeps private thin wrappers where the methods were private.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "display/displaypipelinecontroller.h"
#include "imageview_types.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

QList<ImageItem *> WorkspaceController::collectDoomedItems(
    const QStringList &paths, const QVector<SessionImageId> &sessionIds) const
{
    // Prefer session-id identity. Fall back to path occurrence counts so
    // duplicate paths remain as separate tiles (same path, distinct items).
    QList<ImageItem *> doomed;
    QSet<ImageItem *> doomedSeen;
    auto doom = [&](ImageItem *item) {
        if (!item || doomedSeen.contains(item)) {
            return;
        }
        doomedSeen.insert(item);
        doomed.append(item);
    };

    const QList<ImageItem *> &items = m_view->liveItems();
    const bool haveIds = !sessionIds.isEmpty();
    if (haveIds) {
        QSet<SessionImageId> wantedIds;
        for (SessionImageId id : sessionIds) {
            if (id != kInvalidSessionImageId) {
                wantedIds.insert(id);
            }
        }
        QSet<QString> wantedPaths(paths.begin(), paths.end());
        for (ImageItem *item : items) {
            if (!item) {
                continue;
            }
            const SessionImageId sid = item->sessionId();
            if (sid != kInvalidSessionImageId) {
                if (!wantedIds.contains(sid)) {
                    doom(item);
                }
            } else if (!wantedPaths.contains(item->path())) {
                doom(item);
            }
        }
        // Excess unbound tiles for a path beyond the number of unbound session rows.
        QHash<QString, int> unboundWanted;
        for (int i = 0; i < paths.size(); ++i) {
            const SessionImageId sid = (i < sessionIds.size()) ? sessionIds.at(i)
                                                              : kInvalidSessionImageId;
            if (sid == kInvalidSessionImageId) {
                unboundWanted[paths.at(i)] += 1;
            }
        }
        QHash<QString, int> unboundSeen;
        for (ImageItem *item : items) {
            if (!item || doomedSeen.contains(item)) {
                continue;
            }
            if (item->sessionId() != kInvalidSessionImageId) {
                continue;
            }
            const int n = ++unboundSeen[item->path()];
            if (n > unboundWanted.value(item->path())) {
                doom(item);
            }
        }
    } else {
        QHash<QString, int> wantedCount;
        for (const QString &path : paths) {
            wantedCount[path] += 1;
        }
        QHash<QString, int> seenCount;
        for (ImageItem *item : items) {
            if (!item) {
                continue;
            }
            const int n = ++seenCount[item->path()];
            if (n > wantedCount.value(item->path())) {
                doom(item);
            }
        }
    }
    return doomed;
}

void WorkspaceController::destroyDoomedItems(const QList<ImageItem *> &doomed)
{
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        m_view->hostDisplayPipeline().galleryDecodeResetPath(item->path());
        m_view->hostDisplayPipeline().loadGate().removePendingWorkspacePath(item->path());
        m_view->destroyCanvasItem(item);
    }
}

WorkspaceItemState WorkspaceController::defaultStateForPath(const QString &path,
                                                            int ordinal) const
{
    WorkspaceItemState s;
    s.path = path;
    s.pos = QPointF(40.0 * ordinal, 30.0 * ordinal);
    s.scale = 1.0;
    s.scaleY = 1.0;
    s.rotation = 0.0;
    s.opacity = 1.0;
    s.z = ordinal;
    return s;
}

void WorkspaceController::reorderItemsByPaths(const QStringList &paths,
                                              const QVector<SessionImageId> &ids)
{
    QList<ImageItem *> &items = m_view->liveItems();
    if (items.isEmpty() || paths.isEmpty()) {
        return;
    }
    // Local indexes — findItemBySessionId + path scan per row was O(n²) and ran
    // at the end of every progressive ensurePlaceholders during the size gate.
    QHash<SessionImageId, ImageItem *> bySessionId;
    QMultiHash<QString, ImageItem *> byPath;
    bySessionId.reserve(items.size() * 2);
    byPath.reserve(items.size() * 2);
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        const SessionImageId id = item->sessionId();
        if (id != kInvalidSessionImageId) {
            bySessionId.insert(id, item);
        }
        if (!item->path().isEmpty()) {
            byPath.insert(item->path(), item);
        }
    }
    QList<ImageItem *> ordered;
    ordered.reserve(items.size());
    QSet<ImageItem *> seen;
    // Prefer SessionImageId when parallel ids are present so duplicate paths
    // map to distinct tiles. Path first-unseen is unbound / legacy only.
    const int n = paths.size();
    for (int i = 0; i < n; ++i) {
        ImageItem *picked = nullptr;
        const SessionImageId sid = (i < ids.size()) ? ids.at(i) : kInvalidSessionImageId;
        if (sid != kInvalidSessionImageId) {
            if (ImageItem *byId = bySessionId.value(sid, nullptr)) {
                if (!seen.contains(byId)) {
                    picked = byId;
                }
            }
        }
        if (!picked) {
            const QString &path = paths.at(i);
            const auto range = byPath.equal_range(path);
            for (auto it = range.first; it != range.second; ++it) {
                ImageItem *item = it.value();
                if (!item || seen.contains(item)) {
                    continue;
                }
                picked = item;
                break;
            }
        }
        if (picked) {
            ordered.append(picked);
            seen.insert(picked);
        }
    }
    for (ImageItem *item : items) {
        if (item && !seen.contains(item)) {
            ordered.append(item);
            seen.insert(item);
        }
    }
    if (ordered != items) {
        items = ordered;
        for (int i = 0; i < items.size(); ++i) {
            if (ImageItem *it = items.at(i)) {
                ItemComponents::Placement pl = it->placement();
                pl.z = i;
                it->applyPlacement(pl);
            }
        }
    }
}
