// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstdio>
#include <cstdlib>
#include "imageview.h"
#include "util/biltoo_thread.h"
#include <QScrollBar>
#include <QtMath>
#include "workspace/workspacegeometry.h"
#include "view/viewtransform.h"
#include "session/sessionappearance.h"
#include "util/ttfp_trace.h"
#include "display/imagecache.h"
#include "imageitem.h"
#include "host/imageloader.h"

#include <QSet>
#include <QDebug>
#include <QHash>
#include <QMultiHash>
#include <QPointer>
#include <QTimer>
#include <QUndoStack>
#include <QGraphicsItem>

QList<ImageItem *> ImageView::collectDoomedWorkspaceItems(const QStringList &paths,
                                                          const QVector<SessionImageId> &sessionIds) const
{
    return m_workspace.collectDoomedItems(paths, sessionIds);
}

void ImageView::destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed)
{
    m_workspace.destroyDoomedItems(doomed);
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

    if (isGalleryMode() && hostGallerySizeResolve().active()) {
        // Pack deferred until sizes settle. Keep items hidden so provisional
        // geometry is never painted (cold-open layout glitch).
        if (m_items.isEmpty() && !paths.isEmpty() && !hostGalleryDecodeBook().isDeferPopulate()) {
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
        // Chunked ensure packs once when the last pulse finishes.
        if (!m_gallery.ensurePlaceholders() && !m_items.isEmpty()) {
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
    ASSERT_GUI_THREAD();
    GUI_BUDGET("ImageView::setWorkspacePaths");
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

    // Gallery size-first for every packaged layout (including grid).
    // Gate blocks tiles until the session size set settles.
    if (isGalleryMode() && !paths.isEmpty()
        && hostGallerySizeResolve().startIfNeeded(paths)) {
        TtfpTrace::mark("gallery_size_resolve_await_sizes");
        hostGalleryDecodeBook().setDeferPopulate(true);
    } else {
        hostGalleryDecodeBook().setDeferPopulate(false);
    }

    // Gallery always virtualizes: placeholders + soft/full ladder. The old
    // threshold (80) left smaller PDF/DjVu sessions with *no* tiles — LoadAdd
    // full decode is null for //page: under thumtoo, and soft only upgrades
    // existing items. ImageView/filmstrip still load because they do not depend
    // on this path.
    const bool virtualize = isGalleryMode();

    // Progressive size gate: do not create the full session yet, but seed the
    // ordered prefix that already has definitive sizes (warm memo / book) so
    // masonry/flow can paint immediately — same idea as filmstrip.
    // NEVER hide existing live tiles (stash restore).
    if (isGalleryMode() && hostGalleryDecodeBook().isDeferPopulate()
        && hostGallerySizeResolve().active()) {
        // Seed any already-sized prefix, but stay hidden until gate-complete pack.
        // Showing unstacked items at the origin (previous behaviour after the
        // pack-once change) made the whole session look like one pile.
        (void)m_gallery.ensurePlaceholders();
        finishSetWorkspacePaths(haveIds, paths, sessionIds);
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
                    m_displayPipeline->hostClearDecodedPixels(existing);
                    m_displayPipeline->galleryDecodeResetPath(path);
                    takePendingWorkspacePath(path);

                    PendingSessionBind b;
                    b.path = path;
                    b.id = sid;
                    b.index = i;
                    m_session.bindBook().append(b);
                    if (isGalleryMode()) {
                        m_displayPipeline->scheduleGalleryDecode(path);
                    } else {
                        m_displayPipeline->scheduleImageLoad(path, LoadAdd);
                    }
                } else if (SessionAppearance::hasContentAppearance(app)) {
                    m_displayPipeline->rematerializeItemContent(existing, app);
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
            m_session.bindBook().append(b);
        }

        if (virtualize) {
            // Gallery is viewport-virtualized: path order + size book + layout plan
            // hold the session; ImageItems exist only for the visible window
            // (GalleryController::syncVirtualWindow). Do not create N items here.
            continue;
        } else {
            m_displayPipeline->scheduleImageLoad(path, LoadAdd);
        }
    }
    TtfpTrace::mark("after_createPlaceholders");

    finishSetWorkspacePaths(haveIds, paths, sessionIds);
}



void ImageView::reorderItemsByPaths(const QStringList &paths,
                                    const QVector<SessionImageId> &ids)
{
    m_workspace.reorderItemsByPaths(paths, ids);
}

void ImageView::rebindWorkspaceSession(const QStringList &sessionFiles,
                                       const QVector<SessionImageId> &sessionIds)
{
    m_workspace.rebindSession(sessionFiles, sessionIds);
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    return m_workspace.pathOnLiveCanvas(path);
}

