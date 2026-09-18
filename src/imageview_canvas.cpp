// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include "ttfp_trace.h"
#include "imagecache.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QPointer>
#include <QTimer>

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
        m_pendingWorkspacePaths.remove(item->path());
        destroyCanvasItem(item);
    }
}

void ImageView::finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                        const QVector<SessionImageId> &sessionIds)
{
    TtfpTrace::mark("finishSetWorkspacePaths");
    // Keep canvas order aligned with session/sort order (not async load order).
    reorderItemsByPaths(m_pathOrder);

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
        if (m_items.isEmpty() && !paths.isEmpty() && !m_galleryDeferPopulate) {
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

    m_pathOrder = paths;
    m_sessionIdOrder = sessionIds;
    // Align lengths: missing ids stay invalid (unbound rows).
    while (m_sessionIdOrder.size() < m_pathOrder.size()) {
        m_sessionIdOrder.append(kInvalidSessionImageId);
    }
    while (m_sessionIdOrder.size() > m_pathOrder.size()) {
        m_sessionIdOrder.removeLast();
    }

    // Gallery size-first: probe all unknown sizes before creating tiles so the
    // first pack never uses 1024² stand-ins (first cell stuck square until reload).
    if (isGalleryMode() && !paths.isEmpty()
        && startGallerySizeResolveIfNeeded(paths)) {
        // Probes in flight — pack once in finishGallerySizeResolve with real sizes.
        TtfpTrace::mark("gallery_size_resolve_await_sizes");
        m_galleryDeferPopulate = true;
    } else {
        m_galleryDeferPopulate = false;
    }

    // Gallery always virtualizes: placeholders + soft/full ladder. The old
    // threshold (80) left smaller PDF/DjVu sessions with *no* tiles — LoadAdd
    // full decode is null for //page: under thumtoo, and soft only upgrades
    // existing items. ImageView/filmstrip still load because they do not depend
    // on this path.
    const bool virtualize = isGalleryMode();

    // Cold Gallery: defer all item creation until sizes settle (finish packs once).
    if (isGalleryMode() && m_galleryDeferPopulate && gallerySizeResolveActive()) {
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
                const WorkspaceItemState *appPtr = m_appearance.get(sid);
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
                    m_pendingSessionBinds.append(b);
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
            m_pendingSessionBinds.append(b);
            m_pendingSessionIndexByPath.insert(path, i);
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
    m_pendingScenePos.remove(path);
        gallerySoftResetPath(path);
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}

bool ImageView::addImage(const QString &path)
{
    if (isImageMode()) {
        return false;
    }

    if (ImageItem *existing = findItemByPath(path)) {
        m_scene->clearSelection();
        existing->setSelected(true);
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }

    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageForSession(const QString &path, SessionImageId sessionId,
                                     int sessionIndex)
{
    if (isImageMode() || path.isEmpty()) {
        return false;
    }
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = findItemBySessionId(sessionId)) {
            if (existing->scene() == m_scene) {
                // Re-apply store so an early-return does not leave a bare tile
                // (seed / prior decode without pose or content appearance).
                if (const WorkspaceItemState *app = m_appearance.get(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, *app);
                }
                m_scene->clearSelection();
                existing->setSelected(true);
                ensureVisibleItem(existing);
                emit statusChanged();
                return true;
            }
        }
    }
    if (sessionIndex >= 0) {
        if (ImageItem *existing = findItemBySessionIndex(sessionIndex)) {
            if (sessionId != kInvalidSessionImageId) {
                existing->setSessionId(sessionId);
            }
            if (sessionId != kInvalidSessionImageId) {
                if (const WorkspaceItemState *app = m_appearance.get(sessionId)) {
                    applyStoredAppearance(existing);
                    applyState(existing, *app);
                }
            }
            m_scene->clearSelection();
            existing->setSelected(true);
            ensureVisibleItem(existing);
            emit statusChanged();
            return true;
        }
    }
    // Session-bound add: always decode fresh so content flips/crops of a peer
    // tile with the same path are not shared as baked pixels.
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0) {
        PendingSessionBind b;
        b.path = path;
        b.id = sessionId;
        b.index = sessionIndex;
        m_pendingSessionBinds.append(b);
        if (sessionIndex >= 0) {
            m_pendingSessionIndexByPath.insert(path, sessionIndex);
        }
        // Paste / membership must grow pathOrder so LoadAdd's wanted count
        // includes this session image. Without this, a path already on the
        // canvas left have==pathOrderCount and never created the new tile
        // (filmstrip row only, wrong thumb until appearance emit).
        if (sessionId != kInvalidSessionImageId) {
            bool alreadyOrdered = false;
            for (SessionImageId id : m_sessionIdOrder) {
                if (id == sessionId) {
                    alreadyOrdered = true;
                    break;
                }
            }
            if (!alreadyOrdered) {
                m_pathOrder.append(path);
                m_sessionIdOrder.append(sessionId);
            }
        } else {
            m_pathOrder.append(path);
            m_sessionIdOrder.append(kInvalidSessionImageId);
        }
    }
    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageForSession(const QString &path, int sessionIndex)
{
    return addImageForSession(path, kInvalidSessionImageId, sessionIndex);
}

bool ImageView::addImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty()) {
        return false;
    }
    m_pendingScenePos.insert(path, scenePos);
    return addImage(path);
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                                     SessionImageId sessionId, int sessionIndex)
{
    if (path.isEmpty() || isImageMode()) {
        return false;
    }
    // Same session id already on the canvas: move that tile (do not spawn a twin).
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = findItemBySessionId(sessionId)) {
            if (existing->scene() == m_scene) {
                // Drop out of gallery pack geometry (cell size + pack scale).
                existing->setGalleryCellSize({});
                existing->setItemScale(1.0);
                existing->setItemRotation(0.0);
                existing->setItemShear(0.0);
                existing->setItemOpacity(1.0);
                existing->setPos(scenePos);
                if (isWorkspaceMode()) {
                    existing->setInteractive(true);
                    existing->setScaleHandlesEnabled(true);
                }
                if (m_scene) {
                    m_scene->clearSelection();
                }
                existing->setSelected(true);
                // Persist free-form pose so a later appearance restore cannot
                // revive gallery pack coordinates.
                rememberItemState(existing);
                updateWorkspaceSceneRect();
                ensureVisibleItem(existing);
                emit statusChanged();
                return true;
            }
        }
    }
    // Always decode from disk for a new canvas instance. Never clone another
    // tile's baked pixels by path — that copied crop/flip from the first
    // occurrence and showed the wrong image under a correct filename
    // (IDENTITY: SessionImageId is independent of path).
    PendingSessionBind b;
    b.path = path;
    b.id = sessionId;
    b.index = sessionIndex;
    b.scenePos = scenePos;
    b.hasScenePos = true;
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0 || b.hasScenePos) {
        m_pendingSessionBinds.append(b);
    }
    // Membership order is id-aware: each place of a session image is a row.
    // Path alone cannot express "two tiles, same file".
    if (sessionId != kInvalidSessionImageId) {
        bool alreadyOrdered = false;
        for (SessionImageId id : m_sessionIdOrder) {
            if (id == sessionId) {
                alreadyOrdered = true;
                break;
            }
        }
        if (!alreadyOrdered) {
            m_pathOrder.append(path);
            m_sessionIdOrder.append(sessionId);
        }
    } else {
        // Unbound place: still need a decode slot beyond existing path matches.
        m_pathOrder.append(path);
        m_sessionIdOrder.append(kInvalidSessionImageId);
    }
    // Legacy path-keyed pos kept as fallback when a bind is missing.
    m_pendingScenePos.insert(path, scenePos);

    // Immediate placeholder at the drop point so placement does not depend on
    // async decode ordering (and so archive ladder delays still show a tile).
    // Avoid rememberItemState / selection signals here — dropEvent is still on
    // the stack (filesDropped → handleDroppedUrls); re-entrant status/selection
    // updates were tripping Qt "destructor may have already run" asserts.
    {
        // Workspace scene units = content pixels at scale 1. Placeholder starts
        // at layout size with identity ContentXform (no pack cell, no flips).
        QSize sz = layoutSizeForPath(path, QImage());
        if (!isPositiveSize(sz) || sz.width() <= 1 || sz.height() <= 1) {
            sz = QSize(512, 512);
        }
        ImageItem *ph = new ImageItem(path, sz);
        ph->setPos(scenePos);
        ph->setGalleryCellSize({});
        ph->setItemScale(1.0, 1.0);
        ph->setItemRotation(0.0);
        ph->setItemShear(0.0);
        ph->setItemOpacity(1.0);
        ph->setItemHFlip(false);
        ph->setItemVFlip(false);
        ph->setContentHFlip(false);
        ph->setContentVFlip(false);
        ph->setSessionCrop(false, QRect());
        ph->clearAppliedContentXform();
        if (sessionId != kInvalidSessionImageId) {
            ph->setSessionId(sessionId);
        }
        if (sessionIndex >= 0) {
            ph->setSessionIndex(sessionIndex);
        }
        if (m_scene) {
            m_scene->addItem(ph);
        }
        m_items.append(ph);
        applyItemModeFlags(ph);
        registerItemDisplaySurface(ph);
        if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
            dbg && dbg[0] != '\0' && dbg[0] != '0') {
            fprintf(stderr,
                    "biltoo/drop: placeholder path=%s sid=%lld pos=(%.1f,%.1f) "
                    "size=%dx%d\n",
                    qPrintable(path), static_cast<long long>(sessionId),
                    scenePos.x(), scenePos.y(), sz.width(), sz.height());
        }
    }

    // Defer decode + UI signals until after the drop event stack unwinds.
    const QString pathCopy = path;
    QPointer<ImageView> guard(this);
    QTimer::singleShot(0, this, [guard, pathCopy]() {
        ImageView *const host = guard.data();
        if (!host || !host->isWorkspaceMode()) {
            return;
        }
        host->scheduleImageLoad(pathCopy, LoadAdd);
        host->updateWorkspaceSceneRect();
        emit host->statusChanged();
        // Do not emit workspacePathsChanged here: MainWindow defers
        // syncThumbnailCanvasMembership after the drop; a second rebind race
        // was implicated in Qt type/destructor asserts.
    });
    return true;
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos)
{
    return placeOrMoveImageAt(path, scenePos, kInvalidSessionImageId, -1);
}

void ImageView::selectBySessionIndices(const QList<int> &indices)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    for (int idx : indices) {
        if (ImageItem *item = findItemBySessionIndex(idx)) {
            item->setSelected(true);
        }
    }
}

QList<SessionImageId> ImageView::selectedSessionIds() const
{
    QList<SessionImageId> out;
    for (ImageItem *item : m_items) {
        if (item && item->isSelected() && item->sessionId() != kInvalidSessionImageId) {
            out.append(item->sessionId());
        }
    }
    return out;
}

void ImageView::selectBySessionIds(const QList<SessionImageId> &ids)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = findItemBySessionId(id)) {
            item->setSelected(true);
        }
    }
}

void ImageView::selectPathsByOccurrence(const QStringList &paths)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    QHash<QString, int> nextOccurrence;
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const int want = nextOccurrence.value(path, 0);
        int seen = 0;
        for (ImageItem *item : m_items) {
            if (!item || item->path() != path) {
                continue;
            }
            if (seen == want) {
                item->setSelected(true);
                nextOccurrence[path] = want + 1;
                break;
            }
            ++seen;
        }
    }
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

ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    // Transform targets:
    //   Image → primary (sole) canvas object
    //   Gallery / Workspace → first selected item; Workspace also falls back to
    //   the sole object when the selection is empty
    if (!m_scene) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            // Selection can briefly hold stale pointers after destroyCanvasItem.
            if (!m_items.contains(item) || item->scene() != m_scene) {
                continue;
            }
            return item;
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    return nullptr;
}

bool ImageView::hasTransformTargets() const
{
    return !transformTargets().isEmpty();
}

bool ImageView::hasSingleCropTarget() const
{
    return transformTargets().size() == 1;
}

bool ImageView::validateUniqueLiveSessionIds(const char *context) const
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
    checkList(m_items, "live");
    checkList(m_workspace.stashedItems(), "workspace-stash");
    checkList(m_gallery.stashedItems(), "gallery-stash");
    return ok;
}

void ImageView::ensureGalleryPlaceholders()
{
    if (!isGalleryMode() || m_pathOrder.isEmpty()) {
        return;
    }
    // Only clear defer-populate. Keep size-resolve active so fill layouts still
    // wait for finishGallerySizeResolve to pack (soft may install meanwhile).
    m_galleryDeferPopulate = false;
    QSet<ImageItem *> claimed;
    for (int i = 0; i < m_pathOrder.size(); ++i) {
        const QString &path = m_pathOrder.at(i);
        const SessionImageId sid = (i < m_sessionIdOrder.size())
            ? m_sessionIdOrder.at(i)
            : kInvalidSessionImageId;

        ImageItem *existing = nullptr;
        if (sid != kInvalidSessionImageId) {
            existing = findItemBySessionId(sid);
        }
        if (!existing) {
            for (ImageItem *item : m_items) {
                if (!item || item->path() != path || claimed.contains(item)) {
                    continue;
                }
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
            if (sid != kInvalidSessionImageId
                && existing->sessionId() == kInvalidSessionImageId) {
                existing->setSessionId(sid);
            }
            existing->setSessionIndex(i);
            existing->setVisible(true);
            // Refresh intrinsic from definitive size map.
            const QSize sz = layoutSizeForPath(path, ImageCache::get(path));
            if (isPositiveSize(sz) && !isProvisionalImageSize(path)) {
                existing->setIntrinsicSize(sz);
            }
            continue;
        }

        if (sid != kInvalidSessionImageId || i >= 0) {
            PendingSessionBind b;
            b.path = path;
            b.id = sid;
            b.index = i;
            m_pendingSessionBinds.append(b);
            m_pendingSessionIndexByPath.insert(path, i);
        }

        // Prefer definitive size; soft hint only if still provisional (should be rare).
        const QImage hint = ImageCache::get(path);
        const QSize sz = layoutSizeForPath(path, hint);
        ImageItem *ph = createPlaceholderItem(path, sz);
        if (ph) {
            if (sid != kInvalidSessionImageId) {
                ph->setSessionId(sid);
            }
            ph->setSessionIndex(i);
            ph->setVisible(true);
            if (!hint.isNull()) {
                installDisplayPixels(ph, hint,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     sid);
            }
            claimed.insert(ph);
        }
    }
    reorderItemsByPaths(m_pathOrder);
}
