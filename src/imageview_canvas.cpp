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
        m_displayPipeline.loadGate().removePendingWorkspacePath(item->path());
        destroyCanvasItem(item);
    }
}

void ImageView::finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                        const QVector<SessionImageId> &sessionIds)
{
    TtfpTrace::mark("finishSetWorkspacePaths");
    // Keep canvas order aligned with session/sort order (not async load order).
    reorderItemsByPaths(pathOrderPaths());

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
                if (const WorkspaceItemState *app = appearance().get(sessionId)) {
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
                if (const WorkspaceItemState *app = appearance().get(sessionId)) {
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
        m_bindBook.append(b);
        if (sessionIndex >= 0) {
            m_bindBook.setIndexForPath(path, sessionIndex);
        }
        // Paste / membership must grow pathOrder so LoadAdd's wanted count
        // includes this session image. Without this, a path already on the
        // canvas left have==pathOrderCount and never created the new tile
        // (filmstrip row only, wrong thumb until appearance emit).
        if (sessionId != kInvalidSessionImageId) {
            bool alreadyOrdered = false;
            for (SessionImageId id : pathOrderIds()) {
                if (id == sessionId) {
                    alreadyOrdered = true;
                    break;
                }
            }
            if (!alreadyOrdered) {
                pathOrderAppendRow(path, sessionId);
            }
        } else {
            pathOrderAppendRow(path, kInvalidSessionImageId);
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
    m_displayPipeline.loadGate().setPendingScenePos(path, scenePos);
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
        m_bindBook.append(b);
    }
    // Membership order is id-aware: each place of a session image is a row.
    // Path alone cannot express "two tiles, same file".
    if (sessionId != kInvalidSessionImageId) {
        bool alreadyOrdered = false;
        for (SessionImageId id : pathOrderIds()) {
            if (id == sessionId) {
                alreadyOrdered = true;
                break;
            }
        }
        if (!alreadyOrdered) {
            pathOrderAppendRow(path, sessionId);
        }
    } else {
        // Unbound place: still need a decode slot beyond existing path matches.
        pathOrderAppendRow(path, kInvalidSessionImageId);
    }
    // Legacy path-keyed pos kept as fallback when a bind is missing.
    m_displayPipeline.loadGate().setPendingScenePos(path, scenePos);

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
    m_gallery.ensurePlaceholders();
}


// --- from imageview_layout.cpp (canvas) ---

bool ImageView::hasPendingSessionBindForPath(const QString &path) const
{
    return m_bindBook.hasBindForPath(path);
}


int ImageView::countPendingSessionBinds(const QString &path) const
{
    return m_bindBook.countBindsForPath(path);
}


void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    for (int bi = m_bindBook.bindCount() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_bindBook.bindAt(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_bindBook.removeBindAt(bi);
        }
    }
}


bool ImageView::takePendingSessionBind(const QString &path, PendingSessionBind *out)
{
    return m_bindBook.takeBind(path, out);
}


void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    if (!item || !bound.hasScenePos) {
        return;
    }
    // Explicit drop pose: free-form identity placement only (never revive
    // gallery pack scale/cell or a prior non-uniform footprint scale).
    item->setGalleryCellSize({});
    item->setPos(bound.scenePos);
    item->setItemScale(1.0, 1.0);
    item->setItemRotation(0.0);
    item->setItemShear(0.0);
    item->setItemOpacity(1.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    item->setStackZ(m_items.size() - 1);
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    }
    m_displayPipeline.loadGate().removePendingScenePos(item->path());
    rememberItemState(item);
}


bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    if (!item || image.isNull()) {
        return false;
    }
    // Soft mis-labeled as decoded must still accept a stricter long edge.
    if (item->hasDecodedPixels()
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(image))) {
        return false;
    }
    // Drop / LoadAdd placeholder → full. Never non-uniform scale: that stretched
    // oriented (or just aspect-correct) pixels into the provisional footprint and
    // looked like "wrong rotation + stretch" on drag-drop without any rotate.
    const QSize before = item->imageSize();
    const qreal sx0 = item->itemScaleX();
    const qreal sy0 = item->itemScaleY() > 0.0 ? item->itemScaleY() : sx0;
    const qreal footW = before.width() * sx0;
    const qreal footH = before.height() * sy0;
    // Leave Gallery pack geometry on Workspace tiles.
    item->setGalleryCellSize({});
    installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
                         item->sessionId());
    const QSize after = item->imageSize();
    const bool grew = before.isValid() && after.isValid()
        && (before.width() != after.width() || before.height() != after.height());
    if (grew && isWorkspaceMode() && m_layout.isFreeForm()
        && after.width() > 0 && after.height() > 0) {
        const bool neutralScale =
            qAbs(sx0 - 1.0) < 1e-6 && qAbs(sy0 - 1.0) < 1e-6;
        if (neutralScale) {
            // Fresh drop: 1:1 scene units = content pixels (no footprint squash).
            item->setItemScale(1.0, 1.0);
        } else {
            // Prior intentional scale (e.g. moved from Gallery): fit uniformly.
            const qreal s = ViewTransform::uniformFitScale(
                footW, footH, qreal(after.width()), qreal(after.height()));
            if (s > 1e-6) {
                item->setItemScale(s, s);
            }
        }
        return true;
    }
    if (grew) {
        return true;
    }
    item->update();
    return false;
}


bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    if (!out || path.isEmpty() || !item) {
        return false;
    }
    for (int bi = 0; bi < m_bindBook.bindCount(); ++bi) {
        if (m_bindBook.bindAt(bi).path != path) {
            continue;
        }
        const PendingSessionBind candidate = m_bindBook.bindAt(bi);
        if (candidate.id != kInvalidSessionImageId) {
            if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                if (owner != item) {
                    m_bindBook.removeBindAt(bi);
                    --bi;
                    continue;
                }
            }
        }
        m_bindBook.takeBindAt(bi, out);
        if (out->id != kInvalidSessionImageId) {
            item->setSessionId(out->id);
        }
        if (out->index >= 0 && out->id != kInvalidSessionImageId) {
            item->setSessionIndex(out->index);
        }
        return true;
    }
    return false;
}


void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path,
                                    const QImage &image, bool haveBound,
                                    const PendingSessionBind &bound)
{
    if (!item) {
        return;
    }
    if (isGalleryMode()) {
        // Packed layout owns pose; keep item transform neutral.
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
        item->setItemOpacity(1.0);
        return;
    }
    if (haveBound && bound.hasScenePos) {
        // Explicit drop: place at the drop point (new placement).
        applyPendingBindScenePos(item, bound);
        return;
    }
    if (haveBound && bound.id != kInvalidSessionImageId
        && appearance().get(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        applyState(item, *appearance().get(bound.id));
        return;
    }
    QPointF pos;
    if (m_displayPipeline.loadGate().takePendingScenePos(path, &pos)) {
        item->setPos(pos);
        item->setItemScale(1.0);
        item->setItemRotation(0.0);
        item->setItemOpacity(1.0);
        item->setStackZ(m_items.size() - 1);
        return;
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
        applyState(item, *st);
        return;
    }
    WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
    const QSizeF sz(image.width(), image.height());
    s.pos = findEmptyPlacement(sz);
    applyState(item, s);
}


QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    QList<ImageItem *> doomed;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *item : list) {
            if (item && item->sessionId() == sessionId && !doomed.contains(item)) {
                doomed.append(item);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());
    return doomed;
}


int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            ++n;
        }
    }
    return n;
}


void ImageView::removeWorkspacePathOccurrence(const QString &path, int occurrence)
{
    if (occurrence < 0) {
        return;
    }
    int found = 0;
    for (ImageItem *item : m_items) {
        if (item->path() != path) {
            continue;
        }
        if (found == occurrence) {
            takePendingWorkspacePath(path);
            m_displayPipeline.loadGate().removePendingScenePos(path);
            m_bindBook.removeIndexForPath(path);
        gallerySoftResetPath(path);
destroyCanvasItem(item);
            emit statusChanged();
            emit workspacePathsChanged();
            return;
        }
        ++found;
    }
}


void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (isGalleryMode()) {
        // Open/TTFP: first index is often already in view after pack — ensureVisible
        // on a large scene was hundreds of ms for no visual change.
        bool needScroll = true;
        if (viewport()) {
            const QRectF vis = mapToScene(viewport()->rect()).boundingRect();
            if (vis.isValid() && item->sceneBoundingRect().intersects(vis)) {
                needScroll = false;
            }
        }
        if (needScroll) {
            ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
        }
        // Keyboard focus: show filename in the HUD like mouse hover.
        if (m_gallery.hoverPath() != path) {
            m_gallery.setHoverPath(path);
            viewport()->update();
        }
    }
}


void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, ViewTransform::kEnsureVisibleMargin, ViewTransform::kEnsureVisibleMargin);
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}


void ImageView::destroyCanvasItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    const QString path = item->path();
    unregisterItemDisplaySurface(item);
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_items.contains(item);
    const bool inGalleryStash = m_gallery.stashedItems().contains(item);
    const bool inWorkspaceStash = m_workspace.stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    m_itemInteract.dropIfItem(item);
    if (item == m_gallery.selectionAnchor()) {
        m_gallery.setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (m_groupXform.isScaleDrag() || m_groupXform.isRotateDrag() || !m_groupXform.dragItems.isEmpty()) {
        m_groupXform.endDrag();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_gallery.stashedItems().removeAll(item);
    m_workspace.stashedItems().removeAll(item);

    rememberItemState(item);
    m_items.removeAll(item);
    // Off-canvas neighbor prefetch may still hold a controller for this path.
    if (!path.isEmpty() && !pathOnLiveCanvas(path)) {
        dropTilePrefetchPath(path);
    }
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (m_undoStack && !m_preserveUndoOnDestroy) {
        m_undoStack->clear();
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}


QList<int> ImageView::selectedSessionIndices() const
{
    QList<int> out;
    for (ImageItem *item : m_items) {
        if (item->isSelected() && item->sessionIndex() >= 0) {
            out.append(item->sessionIndex());
        }
    }
    return out;
}


void ImageView::selectAllCanvasItems()
{
    if (!m_scene || isImageMode() || m_items.isEmpty()) {
        return;
    }
    m_scene->blockSignals(true);
    for (ImageItem *item : m_items) {
        if (item) {
            if (isGalleryMode()
                && !(item->flags() & QGraphicsItem::ItemIsSelectable)) {
                item->setGallerySelectable(true);
            }
            item->setSelected(true);
        }
    }
    m_scene->blockSignals(false);
    if (isGalleryMode() && viewport()) {
        viewport()->update();
    }
    if (!m_items.isEmpty()) {
        m_gallery.setSelectionAnchor(m_items.first());
    }
    emit canvasSelectionChanged();
    emit statusChanged();
}


void ImageView::clearCanvasSelection()
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    emit canvasSelectionChanged();
    emit statusChanged();
}


QList<ImageItem *> ImageView::transformTargets() const
{
    QList<ImageItem *> out;
    if (!m_scene) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (isImageMode() || m_items.size() == 1) {
        if (!m_items.isEmpty()) {
            out.append(m_items.first());
        }
    }
    return out;
}


// --- session remove / bind residual from layout ---

QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
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
        m_displayPipeline.loadGate().removePendingWorkspacePath(path);
        gallerySoftResetPath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        m_bindBook.removeIndexForPath(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        destroyCanvasItem(item);
    }
    return removedPaths;
}


void ImageView::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    m_bindBook.removeBindsForSessionId(sessionId);
    for (int i = m_workspace.savedItems().size() - 1; i >= 0; --i) {
        if (m_workspace.savedItems().at(i).sessionId == sessionId) {
            m_workspace.savedItems().removeAt(i);
        }
    }
}


void ImageView::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    if (removedPaths.isEmpty()) {
        return;
    }
    // Rebuild path/id order aligned with remaining tiles (IDENTITY: id first).
    QStringList prunedPaths;
    QVector<SessionImageId> prunedIds;
    prunedPaths.reserve(m_items.size());
    prunedIds.reserve(m_items.size());
    QSet<SessionImageId> seenIds;
    QHash<QString, int> unboundBudget;
    for (ImageItem *item : m_items) {
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
    for (int i = 0; i < pathOrderSize(); ++i) {
        const QString &path = pathOrderPathAt(i);
        const SessionImageId sid = pathOrderIdAt(i);
        if (sid != kInvalidSessionImageId) {
            continue; // already taken from live bound tiles
        }
        if (unboundBudget.value(path) > 0) {
            prunedPaths.append(path);
            prunedIds.append(kInvalidSessionImageId);
            unboundBudget[path] -= 1;
        }
    }
    pathOrderSetOrder(prunedPaths, prunedIds);
}


void ImageView::restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                                  const QPointF &keptCenter, int scrollH, int scrollV)
{
    // Gallery: repack so deleted tiles do not leave empty holes. Preserve the
    // pre-delete viewport centre afterward (same idea as return-from-Image).
    if (gallery && m_scene) {
        if (!m_items.isEmpty()) {
            applyLayout(GalleryPackReason::SessionMutate);
        } else if (keptSceneRect.isValid()) {
            m_scene->setSceneRect(keptSceneRect);
        }
        if (!keptCenter.isNull()) {
            centerOn(keptCenter);
        }
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(scrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(scrollV);
        }
        m_gallery.setViewportSnapshot(keptCenter, scrollH, scrollV);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
}


void ImageView::removeWorkspaceSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    appearance().remove(sessionId);

    // Capture view before any item is destroyed — Qt may shrink sceneRect
    // while removeItem runs, which zeroes scrollbar ranges mid-loop.
    const bool gallery = isGalleryMode();
    QRectF keptSceneRect = (m_scene && gallery) ? m_scene->sceneRect() : QRectF();
    if (gallery && m_scene && !keptSceneRect.isValid()) {
        keptSceneRect = m_scene->itemsBoundingRect();
        if (keptSceneRect.isValid()) {
            keptSceneRect.adjust(-64, -64, 64, 64);
        }
    }
    const QPointF keptCenter = gallery
        ? mapToScene(viewport()->rect().center())
        : QPointF();
    const int scrollH = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int scrollV = verticalScrollBar() ? verticalScrollBar()->value() : 0;

    // Collect first — destroyCanvasItem mutates m_items / stashes.
    const QList<ImageItem *> doomed = collectItemsForSessionId(sessionId);
    const QStringList removedPaths = destroySessionIdItems(doomed);
    prunePendingBindsAndSavedForSessionId(sessionId);
    prunePathOrdersAfterSessionRemove(removedPaths);
    restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH, scrollV);

    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::setCurrentSessionId(SessionImageId id)
{
    if (m_sessionId.currentId == id) {
        return;
    }
    m_sessionId.setCurrentId(id);
    // Attention marker is per SessionImageId — reload draft for the new image.
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.session().clearDraft();
        ensureAttentionPoint();
        if (viewport()) {
            viewport()->update();
        }
    }
}


bool ImageView::hasWorkspaceSessionIndex(int sessionIndex) const
{
    return findItemBySessionIndex(sessionIndex) != nullptr;
}


void ImageView::removeWorkspaceSessionIndex(int sessionIndex)
{
    ImageItem *item = findItemBySessionIndex(sessionIndex);
    if (!item) {
        return;
    }
    // Prefer id-based detach when the tile is bound (duplicate-safe).
    if (item->sessionId() != kInvalidSessionImageId) {
        detachCanvasSessionId(item->sessionId());
        return;
    }
    const QString path = item->path();
    // Only cancel pending work if no other live tile still uses this path.
    bool pathStillLive = false;
    for (ImageItem *other : m_items) {
        if (other && other != item && other->path() == path) {
            pathStillLive = true;
            break;
        }
    }
    if (!pathStillLive) {
        m_displayPipeline.loadGate().removePendingWorkspacePath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        m_bindBook.removeIndexForPath(path);
        gallerySoftResetPath(path);
}
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::detachCanvasSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    // Canvas membership only — keep session appearance and session list entry.
    QList<ImageItem *> doomed;
    for (ImageItem *item : m_items) {
        if (item && item->sessionId() == sessionId) {
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        const QString path = item->path();
        bool pathStillLive = false;
        for (ImageItem *other : m_items) {
            if (other && other != item && other->path() == path) {
                pathStillLive = true;
                break;
            }
        }
        if (!pathStillLive) {
            takePendingWorkspacePath(path);
            m_displayPipeline.loadGate().removePendingScenePos(path);
            m_bindBook.removeIndexForPath(path);
        gallerySoftResetPath(path);
}
        // Drop pending binds for this id only (not every same-path bind).
        m_bindBook.removeBindsForSessionId(sessionId);
        destroyCanvasItem(item);
    }
    if (!doomed.isEmpty()) {
        emit statusChanged();
        emit workspacePathsChanged();
    }
}


void ImageView::bindSelectedSessionIndices(int firstSessionIndex)
{
    if (firstSessionIndex < 0) {
        return;
    }
    int next = firstSessionIndex;
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->setSessionIndex(next);
            ++next;
        }
    }
}


void ImageView::bindSelectedSessionIds(const QList<SessionImageId> &ids)
{
    int i = 0;
    for (ImageItem *item : m_items) {
        if (!item->isSelected()) {
            continue;
        }
        if (i >= ids.size()) {
            break;
        }
        const SessionImageId id = ids.at(i++);
        if (id == kInvalidSessionImageId) {
            continue;
        }
        // Never give the same SessionImageId to two live tiles (drop path used
        // to stamp {sid} onto every selected item while LoadAdd also bound it).
        if (ImageItem *owner = findItemBySessionId(id)) {
            if (owner != item) {
                qCritical("bindSelectedSessionIds: SessionImageId %lld already on another tile — skip",
                          static_cast<long long>(id));
                continue;
            }
        }
        WorkspaceItemState slot;
        if (m_pendingAppearance.take(item, &slot)) {
            // Pending may carry colour grade from Duplicate before the live item
            // was fully synced — apply it so the tile and sessionAppearanceImage match.
            item->setColorAdjustments(slot.colorAdjust);
        } else {
            slot = captureState(item);
        }
        item->setSessionId(id);
        // Live placement from the canvas item (Duplicate offsets, scales, …).
        slot.pos = item->pos();
        slot.scale = item->itemScaleX();
        slot.scaleY = item->itemScaleY();
        slot.shear = item->itemShear();
        slot.rotation = item->itemRotation();
        slot.opacity = item->itemOpacity();
        slot.z = item->stackZ();
        slot.hFlip = item->itemHFlip();
        slot.vFlip = item->itemVFlip();
        slot.hasCrop = item->sessionHasCrop();
        slot.cropRect = item->sessionCropRect();
        slot.contentHFlip = item->contentHFlip();
        slot.contentVFlip = item->contentVFlip();
        slot.colorAdjust = item->colorAdjustments();
        slot.sessionId = id;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        appearance().set(id, slot);
        // Drive ThumbnailBar per-id override (cropped/rotated/graded pixels).
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(id, item->path(), appearance);
            emit sessionCropApplied(id, item->path(), appearance, item->sessionHasCrop());
        }
    }
}

