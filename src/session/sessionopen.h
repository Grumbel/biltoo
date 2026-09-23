// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SESSIONOPEN_H
#define SESSIONOPEN_H

#include "imageview_types.h"

#include <QStringList>
#include <QVector>

class ImageView;
class ThumbnailBar;

/**
 * Shared Open / History session-replace policy.
 *
 * MainWindow still owns expand, sort, index, and chrome; this module holds the
 * ordered barriers so loadFiles and finishApplyExpandedLoad cannot drift apart
 * (IDENTITY / TILE_LOD: drop previous session paints and memos before first
 * paint of the new list).
 */
namespace SessionOpen {

/**
 * First barrier at Open/History start (before expand).
 * Invalidates in-flight loads and clears the filmstrip so old thumbs do not
 * linger while expand or sort runs.
 */
void beginReplace(ImageView *view, ThumbnailBar *filmstrip);

/**
 * Second barrier after expand+sort, before first paint of the new session list.
 * - invalidateSessionLoads again (async expand may have raced the first barrier)
 * - seed path-XDG orient into ItemWorld sparse tables
 * - drop Gallery/Workspace stashes (session open is not a .biltoo project)
 * - optionally clear live Workspace canvas when already in Workspace mode
 * - schedule process size/LQIP/durable-tile memo warm
 *
 * @return whether every path already has a process size memo (sizes "warm").
 */
bool prepareExpandedSession(ImageView *view,
                            const QStringList &paths,
                            const QVector<SessionImageId> &ids,
                            bool clearLiveWorkspace);

/** Schedule Store memo warm (size, LQIP, durable tiles). Non-blocking on GUI. */
void warmProcessMemos(const QStringList &paths);

/**
 * True when every non-empty path has a valid process size memo.
 * Single-image sessions are treated as warm (no Gallery size gate).
 */
bool allSizesInProcessMemo(const QStringList &paths);

} // namespace SessionOpen

#endif // SESSIONOPEN_H
