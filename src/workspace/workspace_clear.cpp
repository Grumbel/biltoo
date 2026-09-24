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
#include "session/packorderview.h"
#include "session/sessionbindbook.h"
#include <QScrollBar>
#include <QWidget>

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

QList<ImageItem *> WorkspaceController::collectItemsForSessionId(SessionImageId sessionId) const
{
    QList<ImageItem *> doomed;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *item : list) {
            if (item && item->sessionId() == sessionId && !doomed.contains(item)) {
                doomed.append(item);
            }
        }
    };
    collect(m_view->liveItems());
    collect(stashedItems());
    collect(m_view->hostGallery().stashedItems());
    return doomed;
}

QStringList WorkspaceController::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    QStringList removedPaths;
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        removedPaths.append(path);
        // Drop in-flight decodes so a late LoadAdd cannot create a tile or
        // call applyLayout after this session image is gone.
        m_view->hostDisplayPipeline().loadGate().removePendingWorkspacePath(path);
        m_view->hostDisplayPipeline().galleryDecodeResetPath(path);
        m_view->hostDisplayPipeline().loadGate().removePendingScenePos(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from live and both stashes (safe if already only in one).
        // persistState=false: caller already removeAppearance for this id —
        // rememberItemState would setAppearance and undo the delete.
        destroyCanvasItem(item, false);
    }
    return removedPaths;
}

void WorkspaceController::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    m_view->hostBindBook().removeBindsForSessionId(sessionId);
    for (int i = savedItems().size() - 1; i >= 0; --i) {
        if (savedItems().at(i).sessionId == sessionId) {
            savedItems().removeAt(i);
        }
    }
}

void WorkspaceController::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    if (removedPaths.isEmpty()) {
        return;
    }
    // Rebuild path/id order aligned with remaining tiles (IDENTITY: id first).
    QStringList prunedPaths;
    QVector<SessionImageId> prunedIds;
    prunedPaths.reserve(m_view->liveItems().size());
    prunedIds.reserve(m_view->liveItems().size());
    QSet<SessionImageId> seenIds;
    QHash<QString, int> unboundBudget;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            if (seenIds.contains(sid)) {
                continue;
            }
            seenIds.insert(sid);
            prunedPaths.append(item->path());
            prunedIds.append(sid);
        } else {
            unboundBudget[item->path()] += 1;
        }
    }
    // Preserve prior order for unbound path slots still live.
    const PackOrderView pack = m_view->currentPackOrder();
    for (int i = 0; i < pack.size(); ++i) {
        const QString path = pack.pathAt(i);
        const SessionImageId sid = pack.idAt(i);
        if (sid != kInvalidSessionImageId) {
            continue; // already taken from live bound tiles
        }
        if (unboundBudget.value(path) > 0) {
            prunedPaths.append(path);
            prunedIds.append(kInvalidSessionImageId);
            unboundBudget[path] -= 1;
        }
    }
    m_view->pathOrderSetOrder(prunedPaths, prunedIds);
}

void WorkspaceController::restoreViewportAfterSessionRemove(
    bool gallery, const QRectF &keptSceneRect, const QPointF &keptCenter,
    int scrollH, int scrollV)
{
    // Gallery: repack so deleted tiles do not leave empty holes. Preserve the
    // pre-delete viewport centre afterward (same idea as return-from-Image).
    if (gallery) {
        QGraphicsScene *scene = m_view->canvasScene();
        if (scene) {
            if (!m_view->liveItems().isEmpty()) {
                m_view->hostGallery().applyLayout(GalleryPackReason::SessionMutate);
            } else if (keptSceneRect.isValid()) {
                scene->setSceneRect(keptSceneRect);
            }
            if (!keptCenter.isNull()) {
                m_view->centerOn(keptCenter);
            }
            if (QScrollBar *h = m_view->horizontalScrollBar()) {
                h->setValue(scrollH);
            }
            if (QScrollBar *v = m_view->verticalScrollBar()) {
                v->setValue(scrollV);
            }
            m_view->hostGallery().setViewportSnapshot(keptCenter, scrollH, scrollV);
        }
    } else if (m_view->isWorkspaceMode()) {
        updateSceneRect();
    }
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}

void WorkspaceController::removeWorkspaceSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    m_view->itemWorld().removeAppearance(sessionId);

    // Capture view before any item is destroyed — Qt may shrink sceneRect
    // while removeItem runs, which zeroes scrollbar ranges mid-loop.
    const bool gallery = m_view->isGalleryMode();
    QGraphicsScene *scene = m_view->canvasScene();
    QRectF keptSceneRect = (scene && gallery) ? scene->sceneRect() : QRectF();
    if (gallery && scene && !keptSceneRect.isValid()) {
        keptSceneRect = scene->itemsBoundingRect();
        if (keptSceneRect.isValid()) {
            keptSceneRect.adjust(-64, -64, 64, 64);
        }
    }
    const QPointF keptCenter = gallery
        ? m_view->mapToScene(m_view->viewport()->rect().center())
        : QPointF();
    const int scrollH = m_view->horizontalScrollBar()
        ? m_view->horizontalScrollBar()->value() : 0;
    const int scrollV = m_view->verticalScrollBar()
        ? m_view->verticalScrollBar()->value() : 0;

    // Collect first — destroyCanvasItem mutates live / stashes.
    const QList<ImageItem *> doomed = collectItemsForSessionId(sessionId);
    const QStringList removedPaths = destroySessionIdItems(doomed);
    prunePendingBindsAndSavedForSessionId(sessionId);
    prunePathOrdersAfterSessionRemove(removedPaths);
    restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH, scrollV);

    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
}
