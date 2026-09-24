// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace path membership helpers used by setWorkspacePaths / bind.
// ImageView keeps private thin wrappers where the methods were private.

#include "workspace/workspacecontroller.h"
#include <QDebug>
#include "item/itemcomponents.h"
#include "imageitem.h"
#include "imageview.h"
#include "item/itemworld.h"
#include "session/sessionbindbook.h"
#include "session/sessionappearance.h"
#include "util/biltoo_thread.h"
#include "gallery/gallerycontroller.h"
#include "imageitem.h"
#include "display/displaypipelinecontroller.h"
#include "imageview_types.h"

#include <QHash>
#include <QSet>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QWidget>
#include <QStringList>
#include <QVector>
#include "util/ttfp_trace.h"

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

void WorkspaceController::rebindSession(const QStringList &sessionFiles,
                                        const QVector<SessionImageId> &sessionIds)
{
    QList<ImageItem *> &items = m_view->liveItems();
    if (sessionFiles.isEmpty()) {
        for (ImageItem *item : items) {
            if (item) {
                item->setSessionIndex(-1);
                // Keep sessionId — still identifies the session image if list is rebuilt.
            }
        }
        return;
    }

    QSet<int> usedIndex;
    QSet<SessionImageId> usedId;
    // Document order for bound ids — O(1) lookup (sessionIndex cache is only a mirror).
    QHash<SessionImageId, int> idToIndex;
    const int n = qMin(sessionFiles.size(), sessionIds.size());
    for (int i = 0; i < n; ++i) {
        const SessionImageId id = sessionIds.at(i);
        if (id != kInvalidSessionImageId) {
            idToIndex.insert(id, i);
        }
    }

    // 1) Prefer stable id: refresh list-order cache from document position.
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            const int found = idToIndex.value(sid, -1);
            if (found < 0) {
                // Id not in current session list — unbound from list order.
                item->setSessionIndex(-1);
                continue;
            }
            if (sessionFiles.at(found) != item->path()) {
                // Id maps to a different path than this tile — do not trust cache.
                qCritical("rebindWorkspaceSession: SessionImageId %lld path mismatch "
                          "(list=%s tile=%s) — clearing list-order cache",
                          static_cast<long long>(sid),
                          qPrintable(sessionFiles.at(found)),
                          qPrintable(item->path()));
                item->setSessionIndex(-1);
                continue;
            }
            // One SessionImageId → at most one live tile. A second claim is
            // corruption (drop id fan-out); unbind so a
            // new session row can be allocated instead of sharing crop/state.
            if (usedId.contains(sid)) {
                qCritical("rebindWorkspaceSession: demoting duplicate live SessionImageId %lld path=%s",
                          static_cast<long long>(sid), qPrintable(item->path()));
                // Unbind: setItemSessionId(invalid) + refresh clears list-order cache.
                m_view->setItemSessionId(item, kInvalidSessionImageId);
                continue;
            }
            m_view->setItemSessionId(item, sid); // refresh list index + applied migrate
            usedIndex.insert(found);
            usedId.insert(sid);
            continue;
        }
        // No id yet — validate legacy index against list path.
        const int si = item->sessionIndex();
        if (si >= 0 && si < sessionFiles.size()
            && sessionFiles.at(si) == item->path() && !usedIndex.contains(si)) {
            usedIndex.insert(si);
            if (si < sessionIds.size()) {
                const SessionImageId listId = sessionIds.at(si);
                if (listId != kInvalidSessionImageId) {
                    m_view->setItemSessionId(item, listId);
                    usedId.insert(listId);
                }
            }
        } else {
            item->setSessionIndex(-1);
        }
    }

    // 2) Assign remaining session rows to unbound canvas items by path occurrence.
    for (int i = 0; i < sessionFiles.size(); ++i) {
        if (usedIndex.contains(i)) {
            continue;
        }
        const QString &path = sessionFiles.at(i);
        const SessionImageId sid = (i < sessionIds.size()) ? sessionIds.at(i)
                                                           : kInvalidSessionImageId;
        for (ImageItem *item : items) {
            if (!item || item->sessionIndex() >= 0) {
                continue;
            }
            if (item->path() != path) {
                continue;
            }
            // Do not steal an item that already has a different stable id.
            if (item->sessionId() != kInvalidSessionImageId
                && sid != kInvalidSessionImageId
                && item->sessionId() != sid) {
                continue;
            }
            // Do not assign an id already owned by another live item.
            if (sid != kInvalidSessionImageId && usedId.contains(sid)) {
                continue;
            }
            item->setSessionIndex(i);
            if (sid != kInvalidSessionImageId) {
                m_view->setItemSessionId(item, sid);
                usedId.insert(sid);
            }
            usedIndex.insert(i);
            break;
        }
    }
    m_view->hostValidateUniqueLiveSessionIds("rebindWorkspaceSession");
}

int WorkspaceController::pathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->path() == path) {
            ++n;
        }
    }
    return n;
}

bool WorkspaceController::pathOnLiveCanvas(const QString &path) const
{
    for (const ImageItem *ii : m_view->liveItems()) {
        if (ii && ii->path() == path) {
            return true;
        }
    }
    return false;
}


void WorkspaceController::selectAllCanvasItems()
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene || m_view->isImageMode() || m_view->liveItems().isEmpty()) {
        return;
    }
    scene->blockSignals(true);
    for (ImageItem *item : m_view->liveItems()) {
        if (item) {
            if (m_view->isGalleryMode()
                && !(item->flags() & QGraphicsItem::ItemIsSelectable)) {
                item->setGallerySelectable(true);
            }
            item->setSelected(true);
        }
    }
    scene->blockSignals(false);
    if (m_view->isGalleryMode()) {
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
    if (!m_view->liveItems().isEmpty()) {
        m_view->hostGallery().setSelectionAnchor(m_view->liveItems().first());
    }
    emit m_view->canvasSelectionChanged();
    emit m_view->statusChanged();
}

void WorkspaceController::finishPathsSet(bool haveIds, const QStringList &paths,
                                         const QVector<SessionImageId> &sessionIds)
{
    TtfpTrace::mark("finishSetWorkspacePaths");
    // Keep canvas order aligned with session/sort order (not async load order).
    const PackOrderView pack = m_view->currentPackOrder();
    reorderItemsByPaths(pack.paths(), pack.ids());

    if (haveIds) {
        rebindSession(paths, sessionIds);
    }

    // Workspace: seed a selection if empty. Gallery must not steal focus to
    // "last item" on layout switch / path refresh (preserves multi-select).
    QGraphicsScene *scene = m_view->canvasScene();
    if (m_view->isWorkspaceMode() && scene && scene->selectedItems().isEmpty()
        && !m_view->liveItems().isEmpty()) {
        m_view->liveItems().last()->setSelected(true);
    }

    GalleryController &gallery = m_view->hostGallery();
    if (m_view->isGalleryMode() && m_view->hostGallerySizeResolve().active()) {
        // Pack deferred until sizes settle. Keep items hidden so provisional
        // geometry is never painted (cold-open layout glitch).
        if (m_view->liveItems().isEmpty() && !paths.isEmpty()
            && !m_view->hostGalleryDecodeBook().isDeferPopulate()) {
            gallery.ensurePlaceholders();
        }
        for (ImageItem *item : m_view->liveItems()) {
            if (item) {
                item->setVisible(false);
            }
        }
        // No decode window until finishGallerySizeResolve packs + shows items.
    } else if (m_view->isGalleryMode() && m_view->liveItems().isEmpty() && !paths.isEmpty()) {
        // Non-fill path should have created items; recover if not.
        // Chunked ensure packs once when the last pulse finishes.
        if (!gallery.ensurePlaceholders() && !m_view->liveItems().isEmpty()) {
            gallery.applyLayout(GalleryPackReason::EnterGallery);
            gallery.updateDecodeWindow();
        }
    } else if (m_view->isGalleryMode() && !m_view->liveItems().isEmpty()) {
        gallery.applyLayout(GalleryPackReason::EnterGallery);
        TtfpTrace::mark("after_applyLayout");
        // Synchronous pass1/pass2 so warm ImageCache installs before first paint.
        // (Deferred-only left cells blank until an explicit relayout.)
        gallery.updateDecodeWindow();
        TtfpTrace::mark("after_updateGalleryDecodeWindow");
        // Viewport often still 0×0 / dock settling; soft jobs land a few ms later.
        // Pulse decode window again so ladderReady installs are not the only path.
        ImageView *view = m_view;
        for (int delay : {0, 50, 200}) {
            QTimer::singleShot(delay, view, [view]() {
                if (view->isGalleryMode() && !view->liveItems().isEmpty()) {
                    view->hostGallery().updateDecodeWindow();
                }
            });
        }
    }

    m_view->hostValidateUniqueLiveSessionIds("setWorkspacePaths");
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
}

void WorkspaceController::setPaths(const QStringList &paths,
                                   const QVector<SessionImageId> &sessionIds)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("WorkspaceController::setPaths");
    if (m_view->isImageMode()) {
        return;
    }

    // Phase 1: cache-only sizes + LQIP so the first pack uses real aspects.
    if (m_view->isGalleryMode() && !paths.isEmpty()) {
        m_view->hostPrimeGalleryGeometryFromCache(paths);
        TtfpTrace::mark("after_primeGalleryGeometryFromCache");
    }

    const bool haveIds = !sessionIds.isEmpty();

    // --- Remove tiles that are not part of the new session -------------------
    destroyDoomedItems(collectDoomedItems(paths, sessionIds));

    // Align lengths: missing ids stay invalid (unbound rows).
    m_view->pathOrderSetOrder(paths, sessionIds);

    // Gallery size-first for every packaged layout (including grid).
    // Gate blocks tiles until the session size set settles.
    if (m_view->isGalleryMode() && !paths.isEmpty()
        && m_view->hostGallerySizeResolve().startIfNeeded(paths)) {
        TtfpTrace::mark("gallery_size_resolve_await_sizes");
        m_view->hostGalleryDecodeBook().setDeferPopulate(true);
    } else {
        m_view->hostGalleryDecodeBook().setDeferPopulate(false);
    }

    // Gallery always virtualizes: placeholders + soft/full ladder. The old
    // threshold (80) left smaller PDF/DjVu sessions with *no* tiles — LoadAdd
    // full decode is null for //page: under thumtoo, and soft only upgrades
    // existing items. ImageView/filmstrip still load because they do not depend
    // on this path.
    const bool virtualize = m_view->isGalleryMode();

    // Progressive size gate: do not create the full session yet, but seed the
    // ordered prefix that already has definitive sizes (warm memo / book) so
    // masonry/flow can paint immediately — same idea as filmstrip.
    // NEVER hide existing live tiles (stash restore).
    if (m_view->isGalleryMode() && m_view->hostGalleryDecodeBook().isDeferPopulate()
        && m_view->hostGallerySizeResolve().active()) {
        // Seed any already-sized prefix, but stay hidden until gate-complete pack.
        // Showing unstacked items at the origin (previous behaviour after the
        // pack-once change) made the whole session look like one pile.
        (void)m_view->hostGallery().ensurePlaceholders();
        finishPathsSet(haveIds, paths, sessionIds);
        return;
    }

    // --- Ensure one live tile per session row (duplicates = separate items) ---
    QSet<ImageItem *> claimed;
    for (int i = 0; i < paths.size(); ++i) {
        const QString &path = paths.at(i);
        const SessionImageId sid = (haveIds && i < sessionIds.size())
            ? sessionIds.at(i)
            : kInvalidSessionImageId;

        ImageItem *existing = nullptr;
        if (sid != kInvalidSessionImageId) {
            existing = m_view->findItemBySessionId(sid);
        }
        if (!existing) {
            // Next unclaimed live tile with this path (occurrence match).
            for (ImageItem *item : m_view->liveItems()) {
                if (!item || item->path() != path || claimed.contains(item)) {
                    continue;
                }
                // Do not steal a tile already bound to a different session id.
                if (sid != kInvalidSessionImageId
                    && item->sessionId() != kInvalidSessionImageId
                    && item->sessionId() != sid) {
                    continue;
                }
                existing = item;
                break;
            }
        }
        if (existing) {
            claimed.insert(existing);
            const bool newlyBoundId = (sid != kInvalidSessionImageId
                                      && existing->sessionId() == kInvalidSessionImageId);
            if (newlyBoundId) {
                m_view->setItemSessionId(existing, sid);
            } else {
                m_view->refreshSessionIndexCache(existing);
            }
            // sessionIndex is session-list order, not pack row (Stage 2 residual).
            if (m_view->sessionListIndex(existing) < 0) {
                existing->setSessionIndex(i);
            }
            if (newlyBoundId && existing->hasDecodedPixels()
                && sid != kInvalidSessionImageId) {
                if (!m_view->itemWorld().hasDurableAppearance(sid)) {
                    continue;
                }
                const WorkspaceItemState app = m_view->sessionAppearanceValue(sid);
                // Crop / content bakes need a full-source redecode; colour grade
                // alone can be applied in place via the central content path.
                if (app.hasCrop || app.contentHFlip || app.contentVFlip
                    || app.contentQuarterTurns != 0) {
                    m_view->hostDisplayPipeline().hostClearDecodedPixels(existing);
                    m_view->hostDisplayPipeline().galleryDecodeResetPath(path);
                    m_view->takePendingWorkspacePath(path);

                    PendingSessionBind b;
                    b.path = path;
                    b.id = sid;
                    b.index = i;
                    m_view->hostBindBook().append(b);
                    if (m_view->isGalleryMode()) {
                        m_view->hostDisplayPipeline().scheduleGalleryDecode(path);
                    } else {
                        m_view->hostDisplayPipeline().scheduleImageLoad(path, ImageView::LoadAdd);
                    }
                } else if (SessionAppearance::hasContentAppearance(app)) {
                    m_view->hostDisplayPipeline().rematerializeItemContent(existing, app);
                }
            }
            continue;
        }

        // No tile for this session row yet — create / schedule one.
        if (sid != kInvalidSessionImageId || i >= 0) {
            PendingSessionBind b;
            b.path = path;
            b.id = sid;
            b.index = i;
            m_view->hostBindBook().append(b);
        }

        if (virtualize) {
            // Gallery is viewport-virtualized: path order + size book + layout plan
            // hold the session; ImageItems exist only for the visible window
            // (GalleryController::syncVirtualWindow). Do not create N items here.
            continue;
        } else {
            m_view->hostDisplayPipeline().scheduleImageLoad(path, ImageView::LoadAdd);
        }
    }
    TtfpTrace::mark("after_createPlaceholders");

    finishPathsSet(haveIds, paths, sessionIds);
}


void WorkspaceController::updateSavedAppearanceFromItem(ImageItem *item)
{
    // Bound durable content is ItemWorld only (Workspace restore re-reads
    // sessionAppearanceValue). Snapshot slots hold pose identity + path —
    // never dual-write crop/orient into m_savedItems (2194 / ECS #5 / 2212).
    if (!item) {
        return;
    }
    const SessionImageId sessionId = item->sessionId();
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    const QString path = item->path();
    const ItemComponents::Placement itemPl = item->placement();
    for (WorkspaceItemState &slot : m_savedItems) {
        if (slot.sessionId != sessionId) {
            continue;
        }
        // Never rewrite another tile's path under the same id (IDENTITY).
        if (!slot.path.isEmpty() && slot.path != path) {
            qCritical("updateSavedAppearanceFromItem: sid %lld slot path %s != %s — skip",
                      static_cast<long long>(sessionId),
                      qPrintable(slot.path), qPrintable(path));
            continue;
        }
        // Full Placement pose from the live item; content stays on ItemWorld
        // sparse tables (same bridge as restore / completeLoadRestore — 2214).
        ItemComponents::applyPlacementToState(slot, itemPl);
        slot.sessionId = sessionId;
        slot.path = path;
    }
}
