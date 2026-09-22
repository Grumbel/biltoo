// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include <QScrollBar>
#include <QtMath>
#include "workspacegeometry.h"
#include "viewtransform.h"
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
        m_displayPipeline.galleryDecodeResetPath(item->path());
        m_displayPipeline.loadGate().removePendingWorkspacePath(item->path());
        destroyCanvasItem(item);
    }
}

void ImageView::finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                        const QVector<SessionImageId> &sessionIds)
{
    TtfpTrace::mark("finishSetWorkspacePaths");
    // Keep canvas order aligned with session/sort order (not async load order).
    const PackOrderView pack = currentPackOrder();
    reorderItemsByPaths(pack.paths(), pack.ids());

    if (haveIds) {
        rebindWorkspaceSession(paths, sessionIds);
    }

    // Workspace: seed a selection if empty. Gallery must not steal focus to
    // "last item" on layout switch / path refresh (preserves multi-select).
    if (isWorkspaceMode() && m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    if (isGalleryMode() && m_gallerySizeResolve.active()) {
        // Pack deferred until sizes settle. Keep items hidden so provisional
        // geometry is never painted (cold-open layout glitch).
        if (m_items.isEmpty() && !paths.isEmpty() && !m_galleryDecodeBook.isDeferPopulate()) {
            m_gallery.ensurePlaceholders();
        }
        for (ImageItem *item : m_items) {
            if (item) {
                item->setVisible(false);
            }
        }
        // No decode window until finishGallerySizeResolve packs + shows items.
    } else if (isGalleryMode() && m_items.isEmpty() && !paths.isEmpty()) {
        // Non-fill path should have created items; recover if not.
        m_gallery.ensurePlaceholders();
        if (!m_items.isEmpty()) {
            m_gallery.applyLayout(GalleryPackReason::EnterGallery);
            m_gallery.updateDecodeWindow();
        }
    } else if (isGalleryMode() && !m_items.isEmpty()) {
        m_gallery.applyLayout(GalleryPackReason::EnterGallery);
        TtfpTrace::mark("after_applyLayout");
        // Synchronous pass1/pass2 so warm ImageCache installs before first paint.
        // (Deferred-only left cells blank until an explicit relayout.)
        m_gallery.updateDecodeWindow();
        TtfpTrace::mark("after_updateGalleryDecodeWindow");
        // Viewport often still 0×0 / dock settling; soft jobs land a few ms later.
        // Pulse decode window again so ladderReady installs are not the only path.
        for (int delay : {0, 50, 200}) {
            QTimer::singleShot(delay, this, [this]() {
                if (isGalleryMode() && !m_items.isEmpty()) {
                    m_gallery.updateDecodeWindow();
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
        && m_gallerySizeResolve.startIfNeeded(paths)) {
        // Probes in flight — pack once in finishGallerySizeResolve with real sizes.
        TtfpTrace::mark("gallery_size_resolve_await_sizes");
        m_galleryDecodeBook.setDeferPopulate(true);
    } else {
        m_galleryDecodeBook.setDeferPopulate(false);
    }

    // Gallery always virtualizes: placeholders + soft/full ladder. The old
    // threshold (80) left smaller PDF/DjVu sessions with *no* tiles — LoadAdd
    // full decode is null for //page: under thumtoo, and soft only upgrades
    // existing items. ImageView/filmstrip still load because they do not depend
    // on this path.
    const bool virtualize = isGalleryMode();

    // Cold Gallery: defer *new* item creation until sizes settle (finish packs
    // once). NEVER hide existing live tiles — that made Gallery look empty after
    // Image/Workspace return (restored stash → setWorkspacePaths → hide forever
    // until finish/cancel). Membership is session; visibility is not a size gate.
    if (isGalleryMode() && m_galleryDecodeBook.isDeferPopulate() && m_gallerySizeResolve.active()) {
        for (ImageItem *item : m_items) {
            if (item && !item->isVisible()) {
                item->setVisible(true);
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
                setItemSessionId(existing, sid);
            } else {
                refreshSessionIndexCache(existing);
            }
            // sessionIndex is session-list order, not pack row (Stage 2 residual).
            if (sessionListIndex(existing) < 0) {
                existing->setSessionIndex(i);
            }
            if (newlyBoundId && existing->hasDecodedPixels()
                && sid != kInvalidSessionImageId) {
                if (!m_itemWorld.hasDurableAppearance(sid)) {
                    continue;
                }
                const WorkspaceItemState app = sessionAppearanceValue(sid);
                // Crop / content bakes need a full-source redecode; colour grade
                // alone can be applied in place via the central content path.
                if (app.hasCrop || app.contentHFlip || app.contentVFlip
                    || app.contentQuarterTurns != 0) {
                    existing->clearDecodedPixels();
                    m_displayPipeline.galleryDecodeResetPath(path);
                    takePendingWorkspacePath(path);

                    PendingSessionBind b;
                    b.path = path;
                    b.id = sid;
                    b.index = i;
                    m_bindBook.append(b);
                    if (isGalleryMode()) {
                        m_displayPipeline.scheduleGalleryDecode(path);
                    } else {
                        m_displayPipeline.scheduleImageLoad(path, LoadAdd);
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
        }

        if (virtualize) {
            // Prefer host ImageCache aspect when native size is still unknown so
            // the first pack does not use a neutral 1000×1000 cell.
            const QImage hint = ImageCache::get(path);
            ImageItem *ph = m_displayPipeline.createPlaceholderItem(
                path, contentLayoutSize(path, sid));
            if (ph) {
                if (sid != kInvalidSessionImageId) {
                    setItemSessionId(ph, sid);
                }
                // List-order cache from document when bound; pack i only unbound hint.
                if (sessionListIndex(ph) < 0) {
                    ph->setSessionIndex(i);
                }
                if (!hint.isNull()) {
                    m_displayPipeline.installDisplayPixels(ph, hint,
                                         SessionAppearance::PixelKind::SoftPreview,
                                         sid);
                }
                claimed.insert(ph);
            }
        } else {
            m_displayPipeline.scheduleImageLoad(path, LoadAdd);
        }
    }
    TtfpTrace::mark("after_createPlaceholders");

    finishSetWorkspacePaths(haveIds, paths, sessionIds);
}



void ImageView::reorderItemsByPaths(const QStringList &paths,
                                    const QVector<SessionImageId> &ids)
{
    if (m_items.isEmpty() || paths.isEmpty()) {
        return;
    }
    QList<ImageItem *> ordered;
    ordered.reserve(m_items.size());
    QSet<ImageItem *> seen;
    // Prefer SessionImageId when parallel ids are present so duplicate paths
    // map to distinct tiles. Path first-unseen is unbound / legacy only.
    const int n = paths.size();
    for (int i = 0; i < n; ++i) {
        ImageItem *picked = nullptr;
        const SessionImageId sid = (i < ids.size()) ? ids.at(i) : kInvalidSessionImageId;
        if (sid != kInvalidSessionImageId) {
            if (ImageItem *byId = findItemBySessionId(sid)) {
                if (!seen.contains(byId)) {
                    picked = byId;
                }
            }
        }
        if (!picked) {
            const QString &path = paths.at(i);
            for (ImageItem *item : m_items) {
                if (!item || item->path() != path || seen.contains(item)) {
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
    for (ImageItem *item : m_items) {
        if (item && !seen.contains(item)) {
            ordered.append(item);
            seen.insert(item);
        }
    }
    if (ordered != m_items) {
        m_items = ordered;
        for (int i = 0; i < m_items.size(); ++i) {
            if (ImageItem *it = m_items.at(i)) {
                ItemComponents::Placement pl = it->placement();
                pl.z = i;
                it->applyPlacement(pl);
            }
        }
    }
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
    for (ImageItem *item : m_items) {
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
                setItemSessionId(item, kInvalidSessionImageId);
                continue;
            }
            setItemSessionId(item, sid); // refresh list index + applied migrate
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
                    setItemSessionId(item, listId);
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
                setItemSessionId(item, sid);
                usedId.insert(sid);
            }
            usedIndex.insert(i);
            break;
        }
    }
    validateUniqueLiveSessionIds("rebindWorkspaceSession");
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    for (const ImageItem *ii : m_items) {
        if (ii && ii->path() == path) {
            return true;
        }
    }
    return false;
}

