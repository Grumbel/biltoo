// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include "sessionappearance.h"
#include "ttfp_trace.h"
#include "imagecache.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QGraphicsItem>

void ImageView::setWorkspacePaths(const QStringList &paths)
{
    setWorkspacePaths(paths, {});
}

QList<ImageItem *> ImageView::collectDoomedWorkspaceItems(const QStringList &paths,
                                                          const QVector<SessionImageId> &sessionIds) const
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

    const bool haveIds = !sessionIds.isEmpty();
    if (haveIds) {
        QSet<SessionImageId> wantedIds;
        for (SessionImageId id : sessionIds) {
            if (id != kInvalidSessionImageId) {
                wantedIds.insert(id);
            }
        }
        QSet<QString> wantedPaths(paths.begin(), paths.end());
        for (ImageItem *item : m_items) {
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
        for (ImageItem *item : m_items) {
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
        for (ImageItem *item : m_items) {
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

void ImageView::destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed)
{
    for (ImageItem *item : doomed) {
        gallerySoftResetPath(item->path());
        m_displayPipeline.loadGate().removePendingWorkspacePath(item->path());
        destroyCanvasItem(item);
    }
}

void ImageView::finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                        const QVector<SessionImageId> &sessionIds)
{
    TtfpTrace::mark("finishSetWorkspacePaths");
    // Keep canvas order aligned with session/sort order (not async load order).
    reorderItemsByPaths(currentPackOrder().paths());

    if (haveIds) {
        rebindWorkspaceSession(paths, sessionIds);
    }

    // Workspace: seed a selection if empty. Gallery must not steal focus to
    // "last item" on layout switch / path refresh (preserves multi-select).
    if (isWorkspaceMode() && m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    if (isGalleryMode() && gallerySizeResolveActive()) {
        // Pack deferred until sizes settle. Keep items hidden so provisional
        // geometry is never painted (cold-open layout glitch).
        if (m_items.isEmpty() && !paths.isEmpty() && !m_gallerySoftBook.isDeferPopulate()) {
            ensureGalleryPlaceholders();
        }
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(false);
            }
        }
        // No decode window until finishGallerySizeResolve packs + shows items.
    } else if (isGalleryMode() && m_items.isEmpty() && !paths.isEmpty()) {
        // Non-fill path should have created items; recover if not.
        ensureGalleryPlaceholders();
        if (!m_items.isEmpty()) {
            applyLayout(GalleryPackReason::EnterGallery);
            updateGalleryDecodeWindow();
        }
    } else if (isGalleryMode() && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::EnterGallery);
        TtfpTrace::mark("after_applyLayout");
        // Synchronous pass1/pass2 so warm ImageCache installs before first paint.
        // (Deferred-only left cells blank until an explicit relayout.)
        updateGalleryDecodeWindow();
        TtfpTrace::mark("after_updateGalleryDecodeWindow");
        // Viewport often still 0×0 / dock settling; soft jobs land a few ms later.
        // Pulse decode window again so ladderReady installs are not the only path.
        for (int delay : {0, 50, 200}) {
            QTimer::singleShot(delay, this, [this]() {
                if (isGalleryMode() && !m_items.isEmpty()) {
                    updateGalleryDecodeWindow();
                }
            });
        }
    }

    validateUniqueLiveSessionIds("setWorkspacePaths");
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::setWorkspacePaths(const QStringList &paths,
                                  const QVector<SessionImageId> &sessionIds)
{
    if (isImageMode()) {
        return;
    }

    // Phase 1: cache-only sizes + LQIP so the first pack uses real aspects.
    if (isGalleryMode() && !paths.isEmpty()) {
        primeGalleryGeometryFromCache(paths);
        TtfpTrace::mark("after_primeGalleryGeometryFromCache");
    }

    const bool haveIds = !sessionIds.isEmpty();

    // --- Remove tiles that are not part of the new session -------------------
    destroyDoomedWorkspaceItems(collectDoomedWorkspaceItems(paths, sessionIds));

    // Align lengths: missing ids stay invalid (unbound rows).
    pathOrderSetOrder(paths, sessionIds);

    // Gallery size-first: probe all unknown sizes before creating tiles so the
    // first pack never uses 1024² stand-ins (first cell stuck square until reload).
    if (isGalleryMode() && !paths.isEmpty()
        && startGallerySizeResolveIfNeeded(paths)) {
        // Probes in flight — pack once in finishGallerySizeResolve with real sizes.
        TtfpTrace::mark("gallery_size_resolve_await_sizes");
        m_gallerySoftBook.setDeferPopulate(true);
    } else {
        m_gallerySoftBook.setDeferPopulate(false);
    }

    // Gallery always virtualizes: placeholders + soft/full ladder. The old
    // threshold (80) left smaller PDF/DjVu sessions with *no* tiles — LoadAdd
    // full decode is null for //page: under thumtoo, and soft only upgrades
    // existing items. ImageView/filmstrip still load because they do not depend
    // on this path.
    const bool virtualize = isGalleryMode();

    // Cold Gallery: defer all item creation until sizes settle (finish packs once).
    if (isGalleryMode() && m_gallerySoftBook.isDeferPopulate() && gallerySizeResolveActive()) {
        // Remove any leftover live items so nothing paints at provisional size.
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(false);
            }
        }
        validateUniqueLiveSessionIds("setWorkspacePaths");
        emit statusChanged();
        emit workspacePathsChanged();
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
            existing = findItemBySessionId(sid);
        }
        if (!existing) {
            // Next unclaimed live tile with this path (occurrence match).
            for (ImageItem *item : m_items) {
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
                existing->setSessionId(sid);
            }
            existing->setSessionIndex(i);
            if (newlyBoundId && existing->hasDecodedPixels()
                && sid != kInvalidSessionImageId) {
                const WorkspaceItemState *appPtr = appearance().get(sid);
                if (!appPtr) {
                    continue;
                }
                const WorkspaceItemState &app = *appPtr;
                // Crop / content bakes need a full-source redecode; colour grade
                // alone can be applied in place via the central content path.
                if (app.hasCrop || app.contentHFlip || app.contentVFlip
                    || app.contentQuarterTurns != 0) {
                    existing->clearDecodedPixels();
                    gallerySoftResetPath(path);
                    takePendingWorkspacePath(path);

                    PendingSessionBind b;
                    b.path = path;
                    b.id = sid;
                    b.index = i;
                    m_bindBook.append(b);
                    if (isGalleryMode()) {
                        scheduleGalleryDecode(path);
                    } else {
                        scheduleImageLoad(path, LoadAdd);
                    }
                } else if (SessionAppearance::hasContentAppearance(app)) {
                    rematerializeItemContent(existing, app);
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
            m_bindBook.append(b);
            m_bindBook.setIndexForPath(path, i);
        }

        if (virtualize) {
            // Prefer host ImageCache aspect when native size is still unknown so
            // the first pack does not use a neutral 1000×1000 cell.
            const QImage hint = ImageCache::get(path);
            ImageItem *ph = createPlaceholderItem(path, layoutSizeForPath(path, hint));
            if (ph) {
                if (sid != kInvalidSessionImageId) {
                    ph->setSessionId(sid);
                }
                ph->setSessionIndex(i);
                if (!hint.isNull()) {
                    installDisplayPixels(ph, hint,
                                         SessionAppearance::PixelKind::SoftPreview,
                                         sid);
                }
                claimed.insert(ph);
            }
        } else {
            scheduleImageLoad(path, LoadAdd);
        }
    }
    TtfpTrace::mark("after_createPlaceholders");

    finishSetWorkspacePaths(haveIds, paths, sessionIds);
}



void ImageView::reorderItemsByPaths(const QStringList &paths)
{
    if (m_items.isEmpty() || paths.isEmpty()) {
        return;
    }
    QList<ImageItem *> ordered;
    ordered.reserve(m_items.size());
    QSet<ImageItem *> seen;
    // One path-order slot → one distinct live item. findItemByPath alone would
    // re-pick the same pointer for duplicate paths and leave m_items with
    // duplicate entries (double-free / UAF on destroy).
    for (const QString &path : paths) {
        for (ImageItem *item : m_items) {
            if (!item || item->path() != path || seen.contains(item)) {
                continue;
            }
            ordered.append(item);
            seen.insert(item);
            break;
        }
    }
    for (ImageItem *item : m_items) {
        if (item && !seen.contains(item)) {
            ordered.append(item);
            seen.insert(item);
        }
    }
    if (ordered != m_items) {
        m_items = ordered;
        for (int i = 0; i < m_items.size(); ++i) {
            if (m_items.at(i)) {
                m_items.at(i)->setStackZ(i);
            }
        }
    }
}

void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    takePendingWorkspacePath(path);
    m_displayPipeline.loadGate().removePendingScenePos(path);
        gallerySoftResetPath(path);
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::rebindWorkspaceSessionIndices(const QStringList &sessionFiles)
{
    // Legacy path-only rebind (no stable ids available).
    rebindWorkspaceSession(sessionFiles, {});
}

void ImageView::rebindWorkspaceSession(const QStringList &sessionFiles,
                                       const QVector<SessionImageId> &sessionIds)
{
    if (sessionFiles.isEmpty()) {
        for (ImageItem *item : m_items) {
            item->setSessionIndex(-1);
            // Keep sessionId — still identifies the session image if list is rebuilt.
        }
        return;
    }

    QSet<int> usedIndex;
    QSet<SessionImageId> usedId;

    // 1) Prefer stable id: refresh list-order cache (sessionIndex) from id position.
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            int found = -1;
            for (int i = 0; i < sessionIds.size() && i < sessionFiles.size(); ++i) {
                if (sessionIds.at(i) == sid) {
                    found = i;
                    break;
                }
            }
            if (found >= 0 && sessionFiles.at(found) == item->path()) {
                // One SessionImageId → at most one live tile. A second claim is
                // corruption (drop bindSelectedSessionIds fan-out); unbind so a
                // new session row can be allocated instead of sharing crop/state.
                if (usedId.contains(sid)) {
                    qCritical("rebindWorkspaceSession: demoting duplicate live SessionImageId %lld path=%s",
                              static_cast<long long>(sid), qPrintable(item->path()));
                    item->setSessionId(kInvalidSessionImageId);
                    item->setSessionIndex(-1);
                    continue;
                }
                item->setSessionIndex(found);
                usedIndex.insert(found);
                usedId.insert(sid);
                continue;
            }
            // Id not in current session list — unbound from list order.
            item->setSessionIndex(-1);
            continue;
        }
        // No id yet — validate legacy index.
        const int si = item->sessionIndex();
        if (si >= 0 && si < sessionFiles.size()
            && sessionFiles.at(si) == item->path() && !usedIndex.contains(si)) {
            usedIndex.insert(si);
            if (si < sessionIds.size()) {
                item->setSessionId(sessionIds.at(si));
                usedId.insert(sessionIds.at(si));
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
        for (ImageItem *item : m_items) {
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
                item->setSessionId(sid);
                usedId.insert(sid);
            }
            usedIndex.insert(i);
            break;
        }
    }
    validateUniqueLiveSessionIds("rebindWorkspaceSession");
}

void ImageView::setPathOrderFromLiveItems()
{
    QStringList paths;
    QVector<SessionImageId> ids;
    paths.reserve(m_items.size());
    ids.reserve(m_items.size());
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        paths.append(item->path());
        ids.append(item->sessionId());
    }
    if (!paths.isEmpty()) {
        pathOrderSetOrder(paths, ids);
    }
}

