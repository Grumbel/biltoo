// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionopen.h"

#include "imageview.h"
#include "thumbnailbar.h"
#include "thumtoocache.h"
#include "ttfp_trace.h"

namespace SessionOpen {

void beginReplace(ImageView *view, ThumbnailBar *filmstrip)
{
    // Cancel in-flight soft/PreferCache for the previous session before expand.
    if (view) {
        view->invalidateSessionLoads();
    }
    // Drop the previous filmstrip immediately so History / Open does not keep
    // showing old session thumbs while expand or async sort runs.
    if (filmstrip) {
        filmstrip->setSession(QStringList(), QVector<SessionImageId>());
    }
}

bool prepareExpandedSession(ImageView *view,
                            const QStringList &paths,
                            const QVector<SessionImageId> &ids,
                            bool clearLiveWorkspace)
{
    // Second barrier after expand/sort: generation may have been bumped at
    // loadFiles start, but expand is async — bump again so jobs from the
    // previous session that finished during expand still cannot install.
    if (view) {
        view->invalidateSessionLoads();
    }
    TtfpTrace::mark("after_invalidateSessionLoads");

    // Pull path-XDG orient/flip/grade into SessionAppearanceStore before first paint.
    if (view) {
        view->seedSessionAppearancesFromPaths(paths, ids);
    }

    // Session open is not a Workspace document. Drop any free-form arrangement
    // so Image↔Workspace does not resurrect previous tiles; only .biltoo
    // projects restore Workspace content.
    if (view) {
        view->discardStashedGallery();
        view->discardStashedWorkspace();
        view->clearDurableWorkspaceSnapshot();
        if (clearLiveWorkspace) {
            view->clearWorkspace();
        }
    }

    TtfpTrace::mark("before_warmSessionOpenMemos");
    warmProcessMemos(paths);
    TtfpTrace::mark("after_warmSessionOpenMemos");

    return allSizesInProcessMemo(paths);
}

void warmProcessMemos(const QStringList &paths)
{
    ThumtooCache::warmSessionOpenMemos(paths);
}

bool allSizesInProcessMemo(const QStringList &paths)
{
    // Single-image / empty: no Gallery size-first gate.
    if (paths.size() <= 1) {
        return true;
    }
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        if (!ThumtooCache::cachedSize(path).isValid()) {
            return false;
        }
    }
    return true;
}

} // namespace SessionOpen
