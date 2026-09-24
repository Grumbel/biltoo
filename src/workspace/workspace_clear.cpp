// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Live-canvas and interaction teardown. ImageView keeps thin routers.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "item/itemworld.h"
#include "image/imagecontroller.h"
#include "host/thumtoocache.h"
#include "tilelod/tile_lod_registry.hpp"
#include "display/tileneighborprefetch.h"
#include "display/imagecache.h"
#include "display/displaypipelinecontroller.h"
#include "imageitem.h"
#include "gallery/gallerycontroller.h"
#include "item/iteminteractsession.h"
#include "util/biltoo_logging.h"

#include <QSet>
#include <QGraphicsScene>
#include <QHash>
#include <QUndoStack>

void WorkspaceController::clearInteractionState()
{
    itemInteract().clear();
    clearGroupTransform();
    m_view->hostGallery().clearChrome();
}

void WorkspaceController::clearLiveCanvas()
{
    // Destroy only the live scene items. Mode stashes (Workspace/Gallery tiles
    // kept while in Image mode) must survive Image-mode LoadReplace / Next.
    clearInteractionState();
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        stack->clear();
    }
    QSet<ImageItem *> protectedStash;
    for (ImageItem *item : stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    for (ImageItem *item : m_view->hostGallery().stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    // Snapshot unique pointers — live list must never hold duplicates, but if it
    // does, destroying by index while mutating the list is unsafe.
    QList<ImageItem *> doomed;
    QSet<ImageItem *> seen;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && !seen.contains(item)) {
            seen.insert(item);
            // Structural: never free a pointer that mode-stash still owns.
            if (protectedStash.contains(item)) {
                biltooModeDbg("clearLiveCanvas SKIP stashed ptr path=%s",
                              qPrintable(item->path()));
                Q_ASSERT_X(false, "clearLiveCanvas",
                           "live list holds a mode-stashed ImageItem* — ownership bug");
                continue;
            }
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        destroyCanvasItem(item);
    }
    m_view->liveItems().clear();
    // Do not scene->clear() — that would delete stashed items if any were
    // still parented (they are not). Scene may hold no items; that is fine.
    m_view->hostChrome().clearMouseInfo();
    emit m_view->mouseInfoChanged(m_view->hostChrome().currentMouseInfo());
}

void WorkspaceController::clearWorkspace()
{
    // Full session/canvas wipe including mode stashes and the durable
    // Workspace snapshot so a subsequent enter() does not resurrect the
    // previous arrangement (project load, session replace).
    clearLiveCanvas();
    discardStash();
    m_view->hostGallery().discardStash();
    savedItems().clear();
    m_view->hostDisplayPipeline().loadGate().clearPending();
    m_view->hostBindBook().clear(); // also clears pendingAppearance
    m_view->hostDisplayPipeline().galleryDecodeResetAll();
    m_view->hostSizeBook().clear();
    m_view->hostGalleryDecodeBook().setDeferPopulate(false);
    m_view->hostGallerySizeResolve().cancel();
    ImageCache::clear();
    m_view->hostTileNeighborPrefetch().clear();
    m_view->hostDisplayPipeline().dropAllTileLodSessions();
    tilelod::TileLodRegistry::instance().invalidateAll();
    ThumtooCache::clearSessionReplaceMemos();
    m_view->pathOrderClear();
    // Path-keyed placement is legacy for unbound tiles only; drop it so a
    // project load cannot inherit stale poses from a previous session.
    m_view->itemWorld().pathBook().clear();
    // SessionDocument::clear only wipes the seed book; sparse ItemWorld tables
    // must be cleared here so hasDurableAppearance cannot see stale ids.
    m_view->itemWorld().clearAppearance();
    m_view->hostImage().clearClassicPath();
    // Invalidate in-flight LoadReplace so a prior Image-mode decode cannot
    // seed the empty Workspace after this wipe (first-path unbound tile).
    m_view->hostDisplayPipeline().loadGate().bumpGeneration();
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->blockSignals(true);
        scene->clear();
        scene->blockSignals(false);
    }
}

bool WorkspaceController::validateUniqueLiveSessionIds(const char *context) const
{
    // Uniqueness is per list. The same SessionImageId on a *live* Image-mode
    // item and a *stashed* Gallery/Workspace tile is intentional (open-from-
    // Gallery keeps the packed tile in the stash while Image edits that id).
    bool ok = true;
    auto checkList = [&](const QList<ImageItem *> &list, const char *where) {
        // Store paths (not item pointers) so the diagnostic never dereferences
        // a hash miss under -Wnull-dereference.
        QHash<SessionImageId, QString> seenPath;
        for (const ImageItem *item : list) {
            if (!item) {
                continue;
            }
            const SessionImageId sid = item->sessionId();
            if (sid == kInvalidSessionImageId) {
                continue;
            }
            const auto it = seenPath.constFind(sid);
            if (it != seenPath.cend()) {
                const QString pathB = item->path();
                qCritical("ImageView: duplicate SessionImageId %lld within %s (%s) path=%s vs %s",
                          static_cast<long long>(sid),
                          where,
                          context ? context : "validate",
                          qPrintable(it.value()),
                          qPrintable(pathB));
                ok = false;
            } else {
                seenPath.insert(sid, item->path());
            }
        }
    };
    checkList(m_view->liveItems(), "live");
    checkList(stashedItems(), "workspace-stash");
    checkList(m_view->hostGallery().stashedItems(), "gallery-stash");
    return ok;
}
