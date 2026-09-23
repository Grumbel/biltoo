// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaypipelinecontroller.h"
#include "item/itemcomponents.h"
#include "display/displaypipeline_jobs.h"

#include "display/tile_load_coordinator.h"

#include "imageview.h"
#include "session/packorderview.h"
#include "imageitem.h"
#include "display/displayedgepolicy.h"
#include "display/pathrasterservice.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "display/lqipdisplaypolicy.h"
#include "biltoo_thread.h"

#include "host/imageloader.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "color/coloradjust.h"
#include "item/imagesizebook.h"
#include "biltoo_logging.h"
#include "ttfp_trace.h"
#include "view/viewtransform.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>
#include <QTimer>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QVarLengthArray>
#include <cstdlib>
#include <cstdio>

void DisplayPipelineController::finishLoadAddStatus(bool refreshGalleryWindow)
{
    emit m_view->statusChanged();
    if (refreshGalleryWindow && m_view->isGalleryMode()) {
        m_view->hostGallery().scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSettleMs);
    }
}


bool DisplayPipelineController::acceptPendingLoadAdd(const QString &path, quint64 generation)
{
    // Mode leave / empty Workspace bumps generation and clears pending paths.
    // Reject superseded gallery window decodes so they cannot spawn tiles on
    // Workspace after the user switched modes mid-decode.
    if (generation != loadGate().generation()) {
        finishLoadAddStatus(/*refreshGalleryWindow=*/false);
        return false;
    }
    if (!loadGate().containsPendingWorkspacePath(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return false;
    }
    m_view->takePendingWorkspacePath(path);
    return true;
}


void DisplayPipelineController::handleLoadAddDecodeFailure(const QString &path)
{
    // //pdfimage: / //page: with thumtoo: empty sync load is expected while the
    // ladder builds — await ladderReady instead of permanent failure.
    if (ThumtooCache::isAvailable()
        && (PagePath::isPdfImageRef(path) || PagePath::isPageRef(path))) {
        ThumtooCache::scheduleProbe(path);
        // Soft state machine will request placeholder / higher steps.
        m_view->hostSessionId().clearLastLoadError();
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return;
    }
    qWarning("ImageView: decode failed for %s", qPrintable(path));
    if (m_view->isGalleryMode()) {
        GalleryDecodeState &st = m_view->hostGalleryDecodeBook().state(path);
        st.failed = true;
        st.inflight = 0;
    }
    m_view->hostSessionId().setLastLoadError(path);
    // Surface the error on any live placeholder for this path.
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            item->setToolTip(m_view->tr("Failed to load:\n%1").arg(path));
        }
    }
    finishLoadAddStatus(/*refreshGalleryWindow=*/true);
}


void DisplayPipelineController::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    for (ImageItem *cand : m_view->hostGallery().stashedItems()) {
        if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
            installDisplayPixels(cand, image,
                                 SessionAppearance::PixelKind::FullSource,
                                 cand->sessionId());
        }
    }
}


void DisplayPipelineController::reassertPendingBindPlacement(const QString &path)
{
    // Drop placeholders created in placeOrMoveImageAt already sit at scenePos;
    // re-assert hasScenePos binds so nothing later drifts them, and so a bind
    // still pairs with the pre-created tile.
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        for (int bi = 0; bi < m_view->hostBindBook().bindCount(); ++bi) {
            const PendingSessionBind &b = m_view->hostBindBook().bindAt(bi);
            if (b.path != path) {
                continue;
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() != kInvalidSessionImageId
                && b.id != item->sessionId()) {
                continue;
            }
            if (b.hasScenePos) {
                item->setGalleryCellSize({});
                {
                    ItemComponents::Placement pl;
                    pl.pos = b.scenePos;
                    item->applyPlacement(pl);
                }
                if (m_view->isWorkspaceMode()) {
                    item->setInteractive(true);
                    item->setScaleHandlesEnabled(true);
                }
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
                m_view->setItemSessionId(item, b.id);
            } else {
                // Already bound: still refresh list-order cache from document.
                m_view->refreshSessionIndexCache(item);
            }
            if (m_view->sessionListIndex(item) < 0 && b.index >= 0) {
                item->setSessionIndex(b.index);
            }
            break;
        }
    }
}


void DisplayPipelineController::claimUnboundItemsForPendingBinds(const QString &path, const QImage &image)
{
    // Claim existing *unbound* tiles of this path for pending session binds
    // (e.g. empty-Workspace LoadReplace seeded the first path before LoadAdd).
    // Without this, have==wanted and the bind is never applied — placement,
    // content flips, and colour grade stay at defaults on that tile.
    for (ImageItem *existing : m_view->liveItems()) {
        if (!existing || existing->path() != path) {
            continue;
        }
        if (existing->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        PendingSessionBind bound;
        if (!m_view->hostBindBook().takeBind(path, &bound)) {
            break;
        }
        if (bound.id != kInvalidSessionImageId) {
            m_view->setItemSessionId(existing, bound.id);
            if (m_view->sessionListIndex(existing) < 0 && bound.index >= 0) {
                existing->setSessionIndex(bound.index);
            }
        } else if (bound.index >= 0) {
            existing->setSessionIndex(bound.index);
        }
        // Raw full decode → single appearance gate (seed tiles may already
        // have decoded defaults without content ops).
        installDisplayPixels(existing, image,
                             SessionAppearance::PixelKind::FullSource,
                             bound.id != kInvalidSessionImageId
                                 ? bound.id
                                 : existing->sessionId());
        if (bound.id != kInvalidSessionImageId && m_view->itemWorld().hasDurableAppearance(bound.id)) {
            m_view->applyState(existing, m_view->sessionAppearanceValue(bound.id));
        }
        // Explicit drop position wins over restored gallery/workspace pose.
        m_view->applyPendingBindScenePos(existing, bound);
        if (bound.id != kInvalidSessionImageId) {
            // Decode must not rewrite filmstrip (sessionAppearanceChanged).
            if (m_view->hostBindBook().removeSelectId(bound.id)) {
                existing->setSelected(true);
            }
        }
    }
}


int DisplayPipelineController::fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                              bool *sizeChangedOut)
{
    bool sizeChanged = false;
    int have = 0;
    const int incoming = ImageCache::longEdge(image);
    for (ImageItem *existing : m_view->liveItems()) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        // Soft was wrongly stored as "decoded"; still accept stricter long edge.
        if (!existing->hasDecodedPixels()
            || existing->shouldUpgradeDisplayTo(incoming)) {
            if (m_view->installFullPreservingWorkspaceFootprint(existing, image)) {
                sizeChanged = true;
            } else if (existing->shouldUpgradeDisplayTo(incoming)) {
                // Footprint helper no-ops once hasDecodedPixels; force upgrade.
                installDisplayPixels(existing, image,
                                     SessionAppearance::PixelKind::FullSource,
                                     existing->sessionId());
                existing->update();
                sizeChanged = true;
            }
        }
    }
    if (sizeChangedOut) {
        *sizeChangedOut = sizeChanged;
    }
    return have;
}


void DisplayPipelineController::createMissingLoadAddItems(const QString &path, const QImage &image,
                                          int have, int wanted)
{
    if (m_view->hostGallerySizeResolve().active() || m_view->hostGalleryDecodeBook().isDeferPopulate()) {
        return;
    }
    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {
        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        PendingSessionBind bound;
        const bool haveBound = m_view->takePendingSessionBindForNewItem(path, item, &bound);
        m_view->applyStoredAppearance(item);
        // Decode/membership must not rewrite filmstrip; user edits emit overrides.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            // Paste: select tiles as they finish decoding.
            if (m_view->hostBindBook().removeSelectId(bound.id)) {
                item->setSelected(true);
            }
        }
        m_view->placeNewLoadAddItem(item, path, image, haveBound, bound);
    }
}


void DisplayPipelineController::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    if (m_view->hostGallerySizeResolve().active() || m_view->hostGalleryDecodeBook().isDeferPopulate()) {
        return;
    }
    if (!m_view->hostLayout().isFreeForm()) {
        if (!m_view->pathOrderIsEmpty()) {
            const PackOrderView pack = m_view->currentPackOrder();
            m_view->reorderItemsByPaths(pack.paths(), pack.ids());
        }
        if (!(m_view->isGalleryMode() && m_view->hostGalleryRelayoutSuppress().active())) {
            if (sizeChanged) {
                m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
            } else {
                m_view->hostGallery().applyLayout(GalleryPackReason::SessionMutate);
            }
        }
    } else {
        m_view->updateWorkspaceSceneRect();
    }
}


void DisplayPipelineController::completeLoadAdd(const QString &path, const QImage &image, quint64 generation)
{
    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window.
    // Duplicate paths are separate session images: fill every undecoded live
    // occurrence, then create until live count matches pathOrder occurrences.
    galleryDecodeResetPath(path);

    // Remember size even when the pending membership was cancelled — a successful
    // decode still updates the session size cache for later layout.
    if (generation == loadGate().generation() && !image.isNull()) {
        m_view->rememberSizeFromDecode(path, image);
    }
    if (!acceptPendingLoadAdd(path, generation)) {
        return;
    }
    if (image.isNull()) {
        handleLoadAddDecodeFailure(path);
        return;
    }

    if (m_view->isImageMode()) {
        // Fill stashed Gallery placeholders while user is in Image mode.
        fillStashedItemsForPath(path, image);
        emit m_view->statusChanged();
        return;
    }

    reassertPendingBindPlacement(path);

    const int pathOrderCount = m_view->pathOrderOccurrences(path);

    // Pending binds whose SessionImageId is already on a live tile are satisfied.
    m_view->purgeSatisfiedPendingBinds(path);

    claimUnboundItemsForPendingBinds(path, image);

    const int pendingBinds = m_view->hostBindBook().countBindsForPath(path);

    bool sizeChanged = false;
    int have = fillLiveItemsWithDecodedPixels(path, image, &sizeChanged);
    fillStashedItemsForPath(path, image);

    // Session pathOrder is the multiplicity source of truth. Do not create more
    // tiles than session rows for this path (pending binds only fill gaps).
    int wanted = pathOrderCount;
    if (wanted <= 0) {
        // Not in session pathOrder (ad-hoc workspace place): one tile per bind.
        wanted = DisplayEdgePolicy::wantedBindCount(have, pendingBinds);
    }

    createMissingLoadAddItems(path, image, have, wanted);
    applyLoadAddLayoutAfterMembership(sizeChanged);

    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSettleMs);
    }
    if (m_view->isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
}

void DisplayPipelineController::scheduleImageLoad(const QString &path, int role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == ImageView::LoadAdd) {
        m_view->hostDisplayPipeline().loadGate().addPendingWorkspacePath(path);
    }
    // ImageView::LoadRestore pending is owned by loadGate().pendingRestoreStates() (AUDIT M27).
    // AUDIT H3a: only ImageView::LoadReplace advances the generation token so workspace
    // adds cannot cancel an in-flight Image-mode navigation decode.
    quint64 gen = loadGate().generation();
    if (role == ImageView::LoadReplace) {
        gen = loadGate().bumpGeneration();
        m_view->hostGalleryDecodeBook().clearImageModeNativeDecode();
        // Do NOT setPrimaryInterest here — that starts EnsureTiles / FocusFull
        // pyramid builds on archives and cancels the soft queue every ←/→.
    }
    // ImageView::LoadReplace: do NOT emit statusChanged — MainWindow finishCurrentIndexChromeUpdate
    // already updateStatus(); a second statusChanged re-entered updateStatus and
    // rebuilt chrome on every ←/→ (statusText, metadata, adjustments, filmstrip pending).

    // Slideshow dual-blit already decoded this path — reuse under the hold.
    if (role == ImageView::LoadReplace && tryDeliverReplaceFromSlideshowRaster(path, gen)) {
        return;
    }

    // Slideshow owns the viewport via pure-phase buffers — never soft-install
    // or PreferCache-climb the underlay ImageItem while the show is running.
    if (role == ImageView::LoadReplace && m_view->isImageMode() && m_view->hostSlideshow().hud().isProgressActive()) {
        biltooLoadDbg("PATH slideshow active skip image-mode load path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
    }

    // Image mode: swap best in-process soft/LQIP immediately (no IPC, no pool).
    if (role == ImageView::LoadReplace && m_view->isImageMode()) {
        installImageModePendingTile(path);
        // Rapid ←/→: stop here. No PreferCache, no classic decode, no escalate —
        // those race the next key and stall the GUI. Settle timer (MainWindow
        // ~80ms quiet) clears nav-hot and calls loadImage again for climb.
        if (m_view->hostSlideshow().hud().isNavHot()) {
            const int edge = imageModeItemForPath(path)
                ? imageModeItemForPath(path)->displayPixelLongEdge()
                : 0;
            biltooLoadDbg("PATH nav-hot soft-only path=%s edge=%d",
                          qPrintable(QFileInfo(path).fileName()), edge);
            return;
        }
        if (ImageItem *it = imageModeItemForPath(path)) {
            if (it->displayPixelLongEdge() > 0) {
                const QImage soft = it->displayImage();
                const QString pathCopy = path;
                // One frame for soft paint, then PreferCache for the settled path.
                QTimer::singleShot(16, m_view, [this, pathCopy, soft]() {
                    if (!m_view->isImageMode() || m_view->hostImage().classicPath() != pathCopy) {
                        return;
                    }
                    if (m_view->hostSlideshow().hud().isNavHot()) {
                        return;
                    }
                    ensureImageModeQualityClimb(pathCopy, soft);
                });
                return;
            }
        }
    }

    // Image mode: do not SoftOnly/PreferCache on cold open — LQIP (if cached)
    // + tiles. Other modes still seed Soft via scheduleClassicImageDecode.

    if (m_view->hostSlideshow().hud().isProgressActive()) {
        scheduleSlideshowReplaceDecode(path, gen, role);
        return;
    }
    scheduleClassicImageDecode(path, gen, role);
}


bool DisplayPipelineController::tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen)
{
    const QImage ready = m_view->hostSlideshow().slideshowRaster(path);
    if (ready.isNull()) {
        return false;
    }
    const QPointer<ImageView> guard(m_view);
    queueImageLoaded(guard, path, ready, gen, static_cast<int>(ImageView::LoadReplace));
    return true;
}


void DisplayPipelineController::scheduleSlideshowReplaceDecode(const QString &path, quint64 gen, int role)
{
    // Soft first (priority), then PreferCache at target edge (low).
    // Key-repeat skips loadImage entirely (MainWindow debounce); this path is
    // for settled index / auto-advance — must climb above soft max or the show
    // stays on thumbnails forever.
    const QPointer<ImageView> guard(m_view);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const int qualityEdge = m_view->hostSlideshow().slideshowTargetEdge();
    const int roleInt = static_cast<int>(role);
    // Snapshot session appearance for the worker (crop is id-keyed, not path).
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);

    // Warm ImageCache / durable tiles: no SoftOnly encode.
    {
        const QImage cached = ImageCache::get(path);
        const int have = ImageCache::longEdge(cached);
        if (have > 0) {
            ImageCache::put(path, cached);
            queuePreviewLoaded(guard, path, cached, gen, roleInt);
            if (ImageCache::adequate(cached, qualityEdge)) {
                queueImageLoaded(guard, path, cached, gen, roleInt);
                return;
            }
        }
        if (ThumtooCache::hasDurableTilesKnown(path)) {
            tickPrimaryTileLod(12);
            if (qualityEdge > softEdge) {
                startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge,
                                       sessionApp);
            }
            return;
        }
        if (have > 0 && ImageCache::adequate(cached, softEdge)) {
            if (qualityEdge > softEdge) {
                startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge,
                                       sessionApp);
            }
            return;
        }
    }

    // Cold only: soft stand-in then quality job.
    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    if (qualityEdge > softEdge) {
        startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge, sessionApp);
    }
}


void DisplayPipelineController::scheduleClassicImageDecode(const QString &path, quint64 gen, int role)
{
    // Image mode LoadReplace: soft underlay + tiles. If the canvas has no
    // display pixels yet (Workspace→Image after clearLiveCanvas), deliver a
    // soft/LQIP sample via queueImageLoaded so completeLoadReplace /
    // tryInstallImageModeSample can create or fill the item. Probe/tiles alone
    // left an empty Image view when ImageCache had no prior soft sample.
    if (m_view->isImageMode() && !m_view->hostSlideshow().hud().isProgressActive()
        && !m_view->hostSlideshow().hud().isNavHot()
        && role == static_cast<int>(ImageView::LoadReplace)) {
        ThumtooCache::scheduleProbe(path);
        ImageItem *it = imageModeItemForPath(path);
        const bool needSoft = !it || !it->hasDisplayPixels()
            || it->displayPixelLongEdge() <= 0;
        if (needSoft) {
            const QPointer<ImageView> guard(m_view);
            const int roleInt = static_cast<int>(role);
            QImage cached = ImageCache::get(path);
            if (!cached.isNull()) {
                queueImageLoaded(guard, path, cached, gen, roleInt);
            } else {
                QThreadPool::globalInstance()->start(
                    [guard, path, roleInt, gen]() {
                        if (!guard || !guard->matchesLoadGeneration(gen)) {
                            return;
                        }
                        QImage preview = loadSoftPreviewPixels(path, 0);
                        if (preview.isNull()) {
                            preview = ImageLoader::loadThumbnail(
                                path, ThumtooCache::kGalleryLadderEdge);
                        }
                        if (preview.isNull()) {
                            return;
                        }
                        ImageCache::put(path, preview);
                        queueImageLoaded(guard, path, preview, gen, roleInt);
                    },
                    2);
            }
        }
        tickPrimaryTileLod(12);
        Q_UNUSED(gen);
        return;
    }

    // Workspace LoadRestore must create tiles via completeLoadRestore. The
    // ordinary Gallery/Workspace path only upgrades existing items (tiles /
    // LQIP); with an empty canvas after durable snapshot rebuild that left
    // Workspace permanently blank after mode switch (tip 2147). Soft preview
    // jobs only queue onImagePreviewLoaded — that path never creates items —
    // so always queueImageLoaded here (cache hit or soft worker).
    if (role == static_cast<int>(ImageView::LoadRestore) && m_view->isWorkspaceMode()) {
        const QPointer<ImageView> guard(m_view);
        const int roleInt = static_cast<int>(role);
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull()) {
            queueImageLoaded(guard, path, cached, gen, roleInt);
            return;
        }
        ThumtooCache::scheduleProbe(path);
        QThreadPool::globalInstance()->start(
            [guard, path, roleInt, gen]() {
                if (!guard || !guard->matchesLoadGeneration(gen)) {
                    return;
                }
                QImage preview = loadSoftPreviewPixels(path, 0);
                if (preview.isNull()) {
                    preview = ImageLoader::loadThumbnail(path, ThumtooCache::kGalleryLadderEdge);
                }
                if (preview.isNull()) {
                    return;
                }
                ImageCache::put(path, preview);
                queueImageLoaded(guard, path, preview, gen, roleInt);
            },
            2);
        return;
    }

    // Gallery / Workspace: LQIP from ImageCache + tiles. Never SoftOnly job.
    if (m_view->isGalleryMode() || m_view->isWorkspaceMode()) {
        ThumtooCache::scheduleProbe(path);
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull()
            && ImageCache::longEdge(cached) <= DisplayQuality::kLqipMaxEdge) {
            const QPointer<ImageView> guard(m_view);
            queuePreviewLoaded(guard, path, cached, gen, static_cast<int>(role));
        }
        if (m_view->isGalleryMode()) {
            scheduleGalleryDecode(path);
        }
        tickPrimaryTileLod(12);
        Q_UNUSED(role);
        return;
    }

    // Fallback (rare non-mode): soft stand-in — prefer cache LQIP first.
    const QPointer<ImageView> guard(m_view);
    const int roleInt = static_cast<int>(role);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);
    {
        const QImage cached = ImageCache::get(path);
        if (!cached.isNull()
            && ImageCache::longEdge(cached) <= DisplayQuality::kLqipMaxEdge) {
            queuePreviewLoaded(guard, path, cached, gen, roleInt);
            tickPrimaryTileLod(8);
            Q_UNUSED(sessionApp);
            return;
        }
    }
    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    Q_UNUSED(gen);
}



void DisplayPipelineController::galleryDecodeResetPath(const QString &path)
{
    m_view->hostGalleryDecodeBook().resetPath(path);
    if (m_view->hostPathRaster() && !path.isEmpty()) {
        m_view->hostPathRaster()->cancel(path);
    }
}


void DisplayPipelineController::galleryDecodeResetAll()
{
    m_view->hostGalleryDecodeBook().clearDecodeStates();
}


int DisplayPipelineController::galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const
{
    // Collect edges for this path, then pure aggregate (LqipDisplayPolicy).
    QVarLengthArray<int, 8> edges;
    QVarLengthArray<bool, 8> decoded;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        edges.append(item->displayPixelLongEdge());
        decoded.append(item->hasDecodedPixels());
    }
    const LqipDisplayPolicy::PathHaveEdge agg =
        LqipDisplayPolicy::aggregatePathHaveEdge(
            edges.constData(), decoded.constData(), edges.size());
    if (anyFullOut) {
        *anyFullOut = agg.anyFull;
    }
    return agg.have;
}



void DisplayPipelineController::scheduleGalleryDecode(const QString &path)
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("scheduleGalleryDecode", 2);
    if (!m_view->isGalleryMode() || path.isEmpty()) {
        return;
    }
    // Gallery: LQIP placeholder + tiles only. Soft PreferCache is removed.
    // Size is ground truth: never install samples or schedule tiles before a
    // definitive native size (or explicit probe failure).
    {
        const ImageSizeBook &book = m_view->hostSizeBook();
        if (book.isFailed(path)) {
            return;
        }
        if (!book.hasDefinitive(path)) {
            m_view->scheduleImageSizeProbe(path);
            return;
        }
    }

    bool anyTileWanted = false;
    bool needLqip = false;
    for (ImageItem *ii : m_view->liveItems()) {
        if (!ii || ii->path() != path) {
            continue;
        }
        if (ii->tileLodWanted()) {
            anyTileWanted = true;
        }
        if (!ii->hasDisplayPixels()) {
            needLqip = true;
        }
    }

    if (needLqip) {
        // ImageCache only (warmSessionOpenMemos / probe workers put LQIP there).
        const QImage host = ImageCache::get(path);
        if (!host.isNull()
            && ImageCache::longEdge(host) <= DisplayQuality::kLqipMaxEdge) {
            for (ImageItem *ii : m_view->liveItems()) {
                if (!ii || ii->path() != path || ii->hasDisplayPixels()) {
                    continue;
                }
                installDisplayPixels(ii, host,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     ii->sessionId());
            }
        }
    }

    GalleryDecodeState &st = m_view->hostGalleryDecodeBook().state(path);
    st.terminal = true; // no soft climb ever
    st.have = GalleryDecode::maxHave(st.have, galleryHaveEdgeFromItems(path, nullptr));

    if (anyTileWanted && !m_view->hostGallerySizeResolve().active()) {
        // Size must be known before pyramid encode (expensive).
        if (!m_view->hostSizeBook().hasDefinitive(path)
            && !ThumtooCache::cachedSize(path).isValid()) {
            return;
        }
        // Only encode a pyramid when Store has no durable coverage yet.
        // Mark queued only after a successful schedule (or durable already
        // known). sizeProbesBusy makes scheduleTilePyramid return false —
        // must not mark queued or this path never retries.
        if (!st.isTilesPyramidQueued()) {
            if (ThumtooCache::hasDurableTilesKnown(path)) {
                st.markTilesPyramidQueued();
            } else if (ThumtooCache::scheduleTilePyramid(path)) {
                st.markTilesPyramidQueued();
            }
        }
    }
}

void DisplayPipelineController::onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                                     int role)
{
    if (image.isNull()) {
        return;
    }
    ImageCache::put(path, image);
    // Soft job during slideshow must upgrade phase buffers (m_ssFrom/To), not
    // only ImageCache — otherwise crossfade stays on empty/LQIP until preload.
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        m_view->hostSlideshow().onSlideshowRasterReady(path, image);
    }

    // Replace navigations: drop superseded previews.
    if (role == ImageView::LoadReplace) {
        if (generation != loadGate().generation() || path != m_view->hostImage().classicPath()) {
            return;
        }
        if (m_view->isImageMode()) {
            // Same install + climb policy as completeImageView::LoadReplace / ladderReady.
            (void)tryInstallImageModeSample(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }

    const int incoming = ImageCache::longEdge(image);

    // Gallery: LQIP placeholder only. Soft PreferCache deliveries must not
    // climb Gallery cells (filmstrip soft used SoftDisplay here).
    if (m_view->isGalleryMode()) {
        if (incoming > 0 && incoming <= DisplayQuality::kLqipMaxEdge) {
            for (ImageItem *item : m_view->liveItems()) {
                if (!item || item->path() != path || item->hasDisplayPixels()) {
                    continue;
                }
                installDisplayPixels(item, image,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     item->sessionId());
            }
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
        }
        return;
    }

    // Workspace: DisplaySurface::decide per item.
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path() != path) {
            continue;
        }
        const bool climbPending =
            m_view->hostPathRaster() && m_view->hostPathRaster()->isClimbPending(path);
        syncItemDisplaySurface(item, incoming, climbPending);
        DisplaySurface::State ds =
            displaySurfaceStateForItem(item, incoming, climbPending);
        const DisplaySurface::SurfaceId sid =
            static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
        const DisplaySurface::Action act =
            (sid != DisplaySurface::kInvalidSurfaceId)
                ? displaySurfaces().evaluate(sid)
                : DisplaySurface::decide(ds);
        const auto pol =
            (!ThumtooCache::hasDurableTilesKnown(path))
                ? PathRasterService::ClimbPolicy::EscalateToFull
                : PathRasterService::ClimbPolicy::SoftDisplay;
        (void)applyDisplaySurfaceAction(item, act, image, ds.needEdge, pol);
    }
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    if (m_view->isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
}


bool DisplayPipelineController::takePendingRestoreState(const QString &path, WorkspaceItemState *out)
{
    return loadGate().takePendingRestoreForPath(path, out);
}


void DisplayPipelineController::completeLoadRestore(const QString &path, const QImage &image)
{
    // LoadRestore is Workspace durable-snapshot rebuild only. Accepting it in
    // Gallery/Image created free-form tiles on the packed canvas and/or ate
    // pending states so Gallery populate looked missing tiles that belong to
    // the session list.
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    WorkspaceItemState state;
    if (!takePendingRestoreState(path, &state) || image.isNull()) {
        return;
    }
    // Do not apply path-keyed crop — restore uses this slot's own state
    // (Workspace duplicates must not inherit another instance's crop).
    ImageItem *item = createItemFromImage(path, image, /*applyStoredSessionCrop=*/false);
    if (!item) {
        return;
    }
    // Prefer live session-image appearance over the leave-mode snapshot when
    // Image-mode edits updated ItemWorld sparse appearance while Workspace was stashed.
    WorkspaceItemState app = state;
    if (state.sessionId != kInvalidSessionImageId) {
        m_view->setItemSessionId(item, state.sessionId);
        if (m_view->itemWorld().hasDurableAppearance(state.sessionId)) {
            app = m_view->sessionAppearanceValue(state.sessionId);
            // Keep placement from the snapshot (pose + display flips + shear).
            ItemComponents::applyPlacementToState(
                app, ItemComponents::placementFromState(state));
        }
    }
    // List-order cache: prefer document position for the bound id; fall back to
    // snapshot index only when the id is unbound / not in the session list.
    if (m_view->refreshSessionIndexCache(item) < 0 && state.sessionIndex >= 0) {
        item->setSessionIndex(state.sessionIndex);
    }
    // Host is in ImageCache / item. Materialize store want (soft stand-in +
    // async multi-MP). Do not bake chrome-only on multi-MP — cannot
    // bake crop on the GUI and used to claim applied == want without pixels.
    {
        const SessionImageId sid = resolveItemSessionId(item, state.sessionId);
        if (sid != kInvalidSessionImageId) {
            app.colorAdjust = m_view->itemWorld().color(sid).grade;
        }
        if (SessionAppearance::hasContentAppearance(app)
            || !app.colorAdjust.isIdentity()
            || (sid != kInvalidSessionImageId && m_view->itemWorld().hasColor(sid))) {
            m_view->rematerializeItemContent(item, app);
        }
    }
    m_view->applyState(item, app);
    if (!m_view->hostLayout().isFreeForm()
        && !(m_view->isGalleryMode() && m_view->hostGalleryRelayoutSuppress().active())) {
        m_view->hostGallery().applyLayout(GalleryPackReason::SessionMutate);
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
}

