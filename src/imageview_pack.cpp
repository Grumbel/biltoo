// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayquality.h"
#include "imageview.h"
#include "biltoo_thread.h"
#include "thumtoocache.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "gallerysoftsm.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QDateTime>
#include <cstdio>
#include <QUndoStack>



void ImageView::scheduleGalleryStatusRefresh(int delayMs)
{
    if (!isGalleryMode()) {
        emit statusChanged();
        return;
    }
    if (!m_galleryStatusRefreshTimer) {
        m_galleryStatusRefreshTimer = new QTimer(this);
        m_galleryStatusRefreshTimer->setSingleShot(true);
        connect(m_galleryStatusRefreshTimer, &QTimer::timeout, this, [this]() {
            if (isGalleryMode()) {
                updateGallerySoftProgressHud();
                emit statusChanged();
            }
        });
    }
    m_galleryStatusRefreshTimer->setInterval(qMax(0, delayMs));
    m_galleryStatusRefreshTimer->start();
}

void ImageView::scheduleGalleryDecodeWindowRefresh(int delayMs)
{
    if (!isGalleryMode()) {
        return;
    }
    if (!m_galleryDecodeScrollTimer) {
        m_galleryDecodeScrollTimer = new QTimer(this);
        m_galleryDecodeScrollTimer->setSingleShot(true);
        connect(m_galleryDecodeScrollTimer, &QTimer::timeout, this, [this]() {
            if (isGalleryMode()) {
                updateGalleryDecodeWindow();
            }
        });
    }
    // Restart with the requested delay (climb uses short; scroll may use longer).
    m_galleryDecodeScrollTimer->setInterval(qMax(0, delayMs));
    m_galleryDecodeScrollTimer->start();
}

int ImageView::galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending)
{
    if (morePending) {
        *morePending = false;
    }
    if (maxInstalls <= 0) {
        return 0;
    }
    int installed = 0;
    // LQIP underlay only — never install soft/HOST whole-frame into Gallery cells.
    QList<ImageItem *> ordered;
    ordered.reserve(m_items.size());
    for (ImageItem *item : m_items) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const int e = item->displayPixelLongEdge();
        if (!item->hasDisplayPixels() || e <= DisplayQuality::kLqipMaxEdge) {
            ordered.prepend(item);
        } else {
            continue; // already past LQIP — tiles own sharpness
        }
    }
    for (ImageItem *item : ordered) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        if (installed >= maxInstalls) {
            if (morePending) {
                *morePending = true;
            }
            break;
        }
        const QString &path = item->path();
        QImage hostSample = ImageCache::get(path);
        if (hostSample.isNull()) {
            hostSample = ThumtooCache::cachedLqipImage(path);
            if (!hostSample.isNull()) {
                ImageCache::put(path, hostSample);
            }
        }
        if (hostSample.isNull()) {
            continue;
        }
        const int hostEdge = ImageCache::longEdge(hostSample);
        const int shown = item->displayPixelLongEdge();
        if (item->hasDisplayPixels() && hostEdge <= shown) {
            continue;
        }
        QImage sample = hostSample;
        int sampleEdge = hostEdge;
        // LQIP underlay only — downscale host soft; never install soft plate.
        if (sampleEdge > DisplayQuality::kLqipMaxEdge) {
            if (item->hasDisplayPixels()) {
                continue;
            }
            const int cap = DisplayQuality::kLqipMaxEdge;
            sample = hostSample.scaled(
                cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            sampleEdge = ImageCache::longEdge(sample);
            if (sample.isNull() || sampleEdge <= 0) {
                continue;
            }
            ImageCache::put(path, sample);
        }
        const SessionAppearance::PixelKind kind =
            SessionAppearance::PixelKind::SoftPreview;
        const int before = shown;
        const bool hadDisplay = item->hasDisplayPixels();
        installDisplayPixels(item, sample, kind, item->sessionId());
        int after = item->displayPixelLongEdge();
        if (after <= before && !hadDisplay && !sample.isNull()) {
            item->setPreviewImage(sample);
            after = item->displayPixelLongEdge();
        }
        if (after <= before && hadDisplay) {
            continue;
        }
        GallerySoftState &st = m_gallerySoft[path];
        // Shown edge only — hostEdge can exceed what install actually attached.
        st.have = qMax(st.have, after);
        item->update();
        ++installed;
    }
    return installed;
}

void ImageView::publishGalleryInterest(const QStringList &interestNear,
                                       const QStringList &interestRest)
{
    // Interest: near ≤ overview 1024; primary = tiles needing >1024 (FocusFull).
    // A primary-only setInterest wiped near/speculative and only EnsureTiles with
    // no host PreferCache install — merge primary into the Gallery snapshot.
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const int ovCap = ThumtooCache::kBatchOverviewEdge;
    const int imgCap = ThumtooCache::kImageLadderEdge;
    int nearEdge = softEdge;
    int primEdge = 0;
    QStringList primary;
    for (const QString &p : interestNear) {
        const auto it = m_gallerySoft.constFind(p);
        if (it == m_gallerySoft.cend() || it->want <= 0) {
            continue;
        }
        nearEdge = qMax(nearEdge, qMin(it->want, ovCap));
        // FocusFull when on-screen need exceeds overview — do not wait for
        // have >= 1024 (PreferCache plateau is often 1024 without tiles).
        if (it->want > ovCap) {
            primary.append(p);
            primEdge = qMax(primEdge, qMin(it->want, imgCap));
        }
    }
    if (primary.size() > 4) {
        primary = primary.mid(0, 4);
    }
    QStringList near = interestNear;
    near.sort();
    QStringList speculative = interestRest;
    speculative.sort();
    primary.sort();
    (void)ThumtooCache::setInterest(near, speculative, nearEdge, softEdge,
                                    primary, primEdge);
}

void ImageView::scheduleIdleGalleryDecodes(const QStringList &rest)
{
    // Speculative off-screen soft only when the visible set is settled.
    // Competing with on-screen PreferCache is the 5–10s Gallery settle storm.
    if (rest.isEmpty() || gallerySoftInflightCount() > 0) {
        return;
    }
    const int freeSlots = galleryDecodeConcurrency();
    if (freeSlots <= 0) {
        return;
    }
    const int idleBudget = qMin(freeSlots, kMaxIdleGalleryDecodes);
    int started = 0;
    for (const QString &path : rest) {
        if (started >= idleBudget) {
            break;
        }
        const int before = gallerySoftInflightCount();
        scheduleGalleryDecode(path);
        if (gallerySoftInflightCount() > before) {
            ++started;
        }
    }
}

GalleryLayout::Mode ImageView::galleryLayoutModeFromViewMode() const
{
    switch (m_layoutMode) {
    case LayoutMode::SideBySide:
        return GalleryLayout::Mode::SideBySide;
    case LayoutMode::Vertical:
        return GalleryLayout::Mode::Vertical;
    case LayoutMode::Grid:
        return GalleryLayout::Mode::Grid;
    case LayoutMode::GridCrop:
        return GalleryLayout::Mode::GridCrop;
    case LayoutMode::Masonry:
        return GalleryLayout::Mode::Masonry;
    case LayoutMode::MasonryRows:
        return GalleryLayout::Mode::MasonryRows;
    case LayoutMode::MasonryFill:
        return GalleryLayout::Mode::MasonryFill;
    case LayoutMode::MasonryRowsFill:
        return GalleryLayout::Mode::MasonryRowsFill;
    case LayoutMode::Flow:
        return GalleryLayout::Mode::Flow;
    case LayoutMode::FlowFill:
        return GalleryLayout::Mode::FlowFill;
    case LayoutMode::Facing:
        return GalleryLayout::Mode::Facing;
    default:
        return GalleryLayout::Mode::Masonry;
    }
}

void ImageView::updateGalleryDecodeWindow()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("updateGalleryDecodeWindow", 4);
    QElapsedTimer decodeWinTimer;
    if (m_perfEnabled) {
        decodeWinTimer.start();
    }
    // -------------------------------------------------------------------------
    // Gallery soft-thumb window (viewport inspection)
    //
    // Pass 1 — ImageCache soft onto blank tiles (budgeted).
    // Pass 2 — candidates via GallerySoftState::needsSoftSchedule + want edge;
    //          scheduleGalleryDecode → PathRasterService::ensure (soft → PreferCache).
    // Pixels: ImageCache / ImageItem. Policy: GallerySoftState + PathRasterService.
    // Image mode full decode is separate.
    // -------------------------------------------------------------------------
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }
    // Fill size-gate used to block all soft until every probe finished — that
    // made cold TTFP = sum of all probes. Placeholders exist; allow soft/LQIP.

    const QRect viewRect = viewport()->rect().adjusted(
        -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
        kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
    const QRectF sceneVisible = mapToScene(viewRect).boundingRect();

    qint64 usPass1 = 0;
    qint64 usPass2 = 0;
    qint64 usInterest = 0;
    QElapsedTimer phaseTimer;

    constexpr int kMaxInstallsPerDecodeWindow = 8;
    bool moreInstallsPending = false;
    if (m_perfEnabled) {
        phaseTimer.start();
    }
    const int hostInstalled =
        galleryInstallHostSoftOntoBlanks(kMaxInstallsPerDecodeWindow,
                                         &moreInstallsPending);
    if (hostInstalled > 0) {
        if (viewport()) {
            viewport()->update();
        }
        scheduleGalleryStatusRefresh(100);
    }
    if (moreInstallsPending) {
        scheduleGalleryDecodeWindowRefresh(32);
    }
    if (m_perfEnabled) {
        usPass1 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    // ------------------------------------------------------------------
    // Pass 2: schedule soft PreferCache / overview climb (O(n), want from this item).
    // ------------------------------------------------------------------
    QStringList visible;
    QStringList rest;
    QStringList interestNear;
    QStringList interestRest;
    QSet<QString> seen;
    constexpr int kMaxSpeculative = 12;
    const int kMaxRestCandidates = kMaxIdleGalleryDecodes * 4;
    const int softCap = ThumtooCache::kGalleryLadderEdge;
    const int filmEdge = ThumtooCache::kFilmstripLadderEdge;

    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QString &path = item->path();
        if (path.isEmpty() || seen.contains(path)) {
            continue;
        }
        seen.insert(path);

        const QRectF tile = item->contentSceneRect();
        const bool tileOk = !tile.isNull() && tile.isValid();
        const bool onScreen = tileOk && tile.intersects(sceneVisible);
        // Cap near list — setInterest used to block GUI for ~1s on ~100 near
        // paths; even async, keep the snapshot small and stable.
        constexpr int kMaxNear = 24;
        if (onScreen && interestNear.size() < kMaxNear) {
            interestNear.append(path);
        } else if (tileOk && interestRest.size() < kMaxSpeculative) {
            interestRest.append(path);
        }

        GallerySoftState &st = m_gallerySoft[path];
        const bool anyFull = item->hasDecodedPixels();
        const bool anyBlank = !item->hasDisplayPixels();
        st.have = qMax(st.have, item->displayPixelLongEdge());
        syncGallerySoftMirrorFromPathRaster(path, st);

        // Fast scroll: free soft slots held by off-screen inflight so newly
        // visible LQIP tiles can schedule. Visible work keeps its inflight.
        if (!onScreen && st.inflight > 0 && st.inflightSinceMs > 0) {
            const qint64 age = QDateTime::currentMSecsSinceEpoch() - st.inflightSinceMs;
            if (age > 1200) {
                clearGallerySoftInflight(st);
            }
        }

        // O(1) want from this item — was galleryWantEdgeForPath O(n) per path.
        int want = filmEdge;
        if (tileOk) {
            const int edge = galleryDisplayEdgeForItem(item, /*allowHighRes=*/onScreen);
            want = onScreen ? edge : qMin(edge, softCap);
        }
        st.want = want;
        // Higher on-screen need than a prior shortfall band → allow reschedule.
        if (st.gaveUpWant > 0 && want > st.gaveUpWant) {
            st.gaveUpWant = 0;
        }

        // Tile LOD band: still queue for scheduleGalleryDecode (probe + tile
        // tick + LQIP install). Never PreferCache soft climb.
        if (item->tileLodWanted()) {
            clearGallerySoftInflight(st);
            if (onScreen || anyBlank) {
                visible.append(path);
            }
            continue;
        }

        if (!st.needsSoftSchedule(want, anyBlank, anyFull)) {
            continue;
        }

        if (onScreen || anyBlank) {
            visible.append(path);
        } else if (rest.size() < kMaxRestCandidates) {
            rest.append(path);
        }
    }

    const int schedBudget =
        qMax(1, galleryDecodeConcurrency() - gallerySoftInflightCount()) + 2;
    int scheduled = 0;
    for (const QString &path : visible) {
        if (scheduled >= schedBudget) {
            scheduleGalleryDecodeWindowRefresh(48);
            break;
        }
        const int before = gallerySoftInflightCount();
        scheduleGalleryDecode(path);
        if (gallerySoftInflightCount() > before) {
            ++scheduled;
        }
    }
    if (m_perfEnabled) {
        usPass2 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    // Speculative interest only when visible soft is idle — otherwise thumtoo
    // spends PreferCache/tiles on off-screen paths while on-screen still climbs.
    const bool visibleBusy = gallerySoftInflightCount() > 0 || !visible.isEmpty();
    publishGalleryInterest(interestNear,
                           visibleBusy ? QStringList{} : interestRest);
    if (m_perfEnabled) {
        usInterest = phaseTimer.nsecsElapsed() / 1000;
    }

    if (!visibleBusy) {
        scheduleIdleGalleryDecodes(rest);
    }
    // Deep-zoom inspection: grid tiles for oversized on-screen cells.
    // Stronger issue budget when any cell is already in the tile band so
    // mid-scroll zoom does not starve tile fetches behind soft concurrency.
    int tileBudget = 6;
    for (ImageItem *ii : m_items) {
        if (ii && ii->tileLodWanted()) {
            tileBudget = 12;
            break;
        }
    }
    tickPrimaryTileLod(tileBudget);
    updateGallerySoftProgressHud();
    if (m_perfEnabled && decodeWinTimer.isValid()) {
        m_perfLastDecodeWindowUs = decodeWinTimer.nsecsElapsed() / 1000;
        m_perfMaxDecodeWindowUs =
            qMax(m_perfMaxDecodeWindowUs, m_perfLastDecodeWindowUs);
        ++m_perfDecodeWindowRuns;
        if (m_perfLastDecodeWindowUs > 4000) {
            fprintf(stderr,
                    "biltoo/perf: updateGalleryDecodeWindow %.1f ms "
                    "(max %.1f ms runs=%d items=%d "
                    "pass1=%.1f pass2=%.1f interest=%.1f install=%d)\n",
                    m_perfLastDecodeWindowUs / 1000.0,
                    m_perfMaxDecodeWindowUs / 1000.0, m_perfDecodeWindowRuns,
                    static_cast<int>(m_items.size()),
                    usPass1 / 1000.0, usPass2 / 1000.0, usInterest / 1000.0,
                    hostInstalled);
        }
    }
}

void ImageView::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts belong only to Gallery; FreeForm only to Workspace.
    if (mode == LayoutMode::FreeForm) {
        if (!isWorkspaceMode()) {
            return;
        }
        if (m_layoutMode != LayoutMode::FreeForm) {
            // Should not happen in Workspace (always FreeForm).
        }
        m_layoutMode = LayoutMode::FreeForm;
        for (ImageItem *item : m_items) {
            applyItemModeFlags(item);
        }
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-64, -64, 64, 64));
        }
        m_fitMode = false;
        emit statusChanged();
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!isGalleryMode()) {
        enterGallery(mode);
        return;
    }

    if (m_layoutMode == LayoutMode::FreeForm && mode != LayoutMode::FreeForm) {
        snapshotFreeFormStates();
    }

    m_layoutMode = mode;
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
    }
    applyLayout(GalleryPackReason::EnterGallery);
}

void ImageView::setGridColumns(int columns)
{
    const int clamped = qMax(0, columns); // 0 = automatic
    if (clamped == m_gridColumns) {
        return;
    }
    m_gridColumns = clamped;
    if (isGalleryMode()
        && (m_layoutMode == LayoutMode::Grid || m_layoutMode == LayoutMode::GridCrop
            || m_layoutMode == LayoutMode::Flow || m_layoutMode == LayoutMode::FlowFill
            || m_layoutMode == LayoutMode::Facing)) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryColumns(int columns)
{
    const int clamped = qBound(1, columns, 32);
    if (clamped == m_masonryColumns) {
        return;
    }
    m_masonryColumns = clamped;
    if ((m_layoutMode == LayoutMode::Masonry || m_layoutMode == LayoutMode::MasonryFill)
        && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryRows(int rows)
{
    const int clamped = qBound(1, rows, 32);
    if (clamped == m_masonryRows) {
        return;
    }
    m_masonryRows = clamped;
    if ((m_layoutMode == LayoutMode::MasonryRows || m_layoutMode == LayoutMode::MasonryRowsFill)
        && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setGalleryRelayoutSuppressed(bool on)
{
    if (on) {
        ++m_galleryRelayoutSuppressCount;
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
    } else if (m_galleryRelayoutSuppressCount > 0) {
        --m_galleryRelayoutSuppressCount;
    }
}

void ImageView::reloadFromDisk(bool relayoutGallery)
{
    if (isImageMode()) {
        if (!hasClassicPath()) {
            return;
        }
        // Force a fresh decode of the focused session image only.
        scheduleImageLoad(classicPath(), LoadReplace);
        flashHud(tr("Reload"), QFileInfo(classicPath()).fileName());
        return;
    }

    // Gallery / Workspace: re-decode every on-canvas item in place.
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        gallerySoftResetPath(path);
        
        
        takePendingWorkspacePath(path);
        item->clearDecodedPixels();
        PendingSessionBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        m_pendingSessionBinds.append(b);
        if (isGalleryMode()) {
            scheduleGalleryDecode(path);
        } else {
            scheduleImageLoad(path, LoadAdd);
        }
    }
    if (isGalleryMode() && relayoutGallery) {
        applyLayout(GalleryPackReason::Reload);
    }
    flashHud(tr("Reload"),
             isGalleryMode() ? tr("Gallery") : tr("Workspace"));
    emit statusChanged();
}

void ImageView::applyLayout(GalleryPackReason reason)
{
    ASSERT_GUI_THREAD();
    if (m_applyingLayout) {
        return;
    }
    // Size-first open: do not pack on provisional stand-ins while probes run.
    if (m_gallerySizeResolveActive) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    if (!isGalleryMode() || m_items.isEmpty() || m_layoutMode == LayoutMode::FreeForm) {
        return;
    }

    if (!m_pathOrder.isEmpty()) {
        reorderItemsByPaths(m_pathOrder);
    }

    // Gallery overview is axis-aligned. Strip any leftover Workspace placement
    // tilt/flips before packing (content 90°/flip remain in baked pixels).
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        item->setItemRotation(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
    }

    // Incremental packs (new session tiles, decode size change, F5) should keep
    // the user roughly in the same place. Enter / explicit layout switch still
    // starts at the origin. Image→Gallery restore uses pendingRestore instead.
    const bool preserveView =
        !m_gallery.pendingRestore()
        && (reason == GalleryPackReason::ContentChange
            || reason == GalleryPackReason::SessionMutate
            || reason == GalleryPackReason::Reload);
    // Scene coordinates are rewritten by pack — do NOT centerOn a pre-pack
    // scene point (that jumped the overview to the middle after crop). Keep
    // scrollbar pixel values instead.
    const int keptScrollH =
        (preserveView && horizontalScrollBar()) ? horizontalScrollBar()->value() : -1;
    const int keptScrollV =
        (preserveView && verticalScrollBar()) ? verticalScrollBar()->value() : -1;

    m_applyingLayout = true;

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    if (!m_gallery.pendingRestore() && !preserveView) {
        centerOn(0, 0);
    }

    // Reserve scrollbar space for the pack measurement. AsNeeded would let the
    // first bar appear, shrink the viewport, and leave the fitted axis slightly
    // oversized (dual bars). AlwaysOn only for this critical section; policy is
    // restored after sceneRect is set so Zoom Fit/Fill can hide unused bars.
    // m_applyingLayout is already true — resizeEvent will not re-enter pack.
    const auto savedHBar = horizontalScrollBarPolicy();
    const auto savedVBar = verticalScrollBarPolicy();
    if (savedHBar != Qt::ScrollBarAlwaysOn || savedVBar != Qt::ScrollBarAlwaysOn) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    const qreal margin = 16.0;
    const qreal gap = 12.0;
    const qreal availW = qMax(32.0, static_cast<qreal>(viewport()->width()) - 2.0 * margin);
    const qreal availH = qMax(32.0, static_cast<qreal>(viewport()->height()) - 2.0 * margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_masonryColumns;
    params.gridColumns = m_gridColumns;
    params.masonryRows = m_masonryRows;
    params.mode = galleryLayoutModeFromViewMode();

    GalleryLayout::pack(m_items, params, [this](ImageItem *item) {
        m_itemStates.insert(item->path(), captureState(item));
    });

    const QRectF bounds = m_scene->itemsBoundingRect().adjusted(-margin, -margin, margin, margin);
    if (m_scene->sceneRect() != bounds) {
        m_scene->setSceneRect(bounds);
    }
    // Restore caller policy (AsNeeded/Off). With overshoot correction the packed
    // fitted axis should not need a bar; AsNeeded can hide it. Still under
    // m_applyingLayout so a policy-driven resize does not repack.
    if (horizontalScrollBarPolicy() != savedHBar) {
        setHorizontalScrollBarPolicy(savedHBar);
    }
    if (verticalScrollBarPolicy() != savedVBar) {
        setVerticalScrollBarPolicy(savedVBar);
    }
    m_fitMode = true;
    // Keep the guard until after statusChanged so slots cannot re-enter layout.
    emit statusChanged();
    m_applyingLayout = false;
    // Re-apply scroll after centerOn(0,0) above when returning from Image.
    applyPendingGalleryRestore();
    if (preserveView) {
        if (keptScrollH >= 0 && horizontalScrollBar()) {
            horizontalScrollBar()->setValue(keptScrollH);
        }
        if (keptScrollV >= 0 && verticalScrollBar()) {
            verticalScrollBar()->setValue(keptScrollV);
        }
    }
    // Explicit column changes: debounce setInterest (was multi-second stalls).
    // EnterGallery / Reload: run decode once now so startup is not blank until
    // the 180ms timer; still schedule a short follow-up for late soft.
    if (reason == GalleryPackReason::ExplicitLayout) {
        scheduleGalleryDecodeWindowRefresh(180);
    } else if (reason == GalleryPackReason::EnterGallery
               || reason == GalleryPackReason::Reload) {
        updateGalleryDecodeWindow();
        scheduleGalleryDecodeWindowRefresh(48);
    } else {
        updateGalleryDecodeWindow();
    }
}

bool ImageView::layoutWorkspaceItems(const GalleryLayout::Params &userParams,
                                     const QList<ImageItem *> &itemsIn)
{
    if (!isWorkspaceMode() || !m_scene) {
        return false;
    }
    QList<ImageItem *> items = itemsIn;
    if (items.isEmpty()) {
        items = transformTargets();
    }
    // Require an explicit multi-item or single selection on the canvas —
    // do not fall back to “sole item” when nothing is selected in Workspace.
    if (items.isEmpty()) {
        for (QGraphicsItem *gi : m_scene->selectedItems()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                items.append(ii);
            }
        }
    }
    if (items.isEmpty()) {
        return false;
    }

    // Preserve selection centroid so the group does not jump to the origin.
    QRectF beforeBounds;
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        beforeBounds = beforeBounds.isNull() ? item->sceneBoundingRect()
                                             : beforeBounds.united(item->sceneBoundingRect());
    }
    const QPointF beforeCenter = beforeBounds.isNull()
        ? mapToScene(viewport()->rect().center())
        : beforeBounds.center();

    QVector<WorkspaceItemState> befores;
    befores.reserve(items.size());
    for (ImageItem *item : items) {
        befores.append(captureState(item));
    }

    GalleryLayout::Params params = userParams;
    const qreal margin = params.margin > 0 ? params.margin : 16.0;
    params.margin = margin;
    if (params.gap <= 0) {
        params.gap = 12.0;
    }
    params.availW = qMax(32.0, static_cast<qreal>(viewport()->width()) - 2.0 * margin);
    params.availH = qMax(32.0, static_cast<qreal>(viewport()->height()) - 2.0 * margin);

    // Workspace layout is axis-aligned placement; clear free-form tilt/flips
    // on the targets only (content bakes stay in pixels).
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        item->setItemRotation(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
    }

    GalleryLayout::pack(items, params);

    // Translate packed group so its centre matches the previous selection centre.
    QRectF afterBounds;
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        afterBounds = afterBounds.isNull() ? item->sceneBoundingRect()
                                           : afterBounds.united(item->sceneBoundingRect());
    }
    if (!afterBounds.isNull()) {
        const QPointF delta = beforeCenter - afterBounds.center();
        if (!delta.isNull()) {
            for (ImageItem *item : items) {
                if (item) {
                    item->setPos(item->pos() + delta);
                }
            }
        }
    }

    if (m_undoStack) {
        m_undoStack->beginMacro(tr("Layout selection"));
        for (int i = 0; i < items.size(); ++i) {
            ImageItem *item = items.at(i);
            if (!item) {
                continue;
            }
            pushItemGeometryCommand(tr("Layout selection"), item, befores.at(i),
                                    captureState(item));
        }
        m_undoStack->endMacro();
    }

    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        if (item->sessionId() != kInvalidSessionImageId) {
            m_appearance.set(item->sessionId(), captureState(item));
        }
        m_itemStates.insert(item->path(), captureState(item));
    }

    updateWorkspaceSceneRect();
    viewport()->update();
    emit statusChanged();
    return true;
}


void ImageView::updateGallerySoftProgressHud()
{
    if (!isGalleryMode()) {
        return;
    }
    // LQIP is a free durable placeholder, not a user-facing "preview stage".
    // Never show "Improving previews… LQIP" — that was noise and mis-sold the product.
    if (m_centreProgressTitle.startsWith(tr("Improving previews"))) {
        clearCentreProgress();
    }
}

void ImageView::gallerySoftWatchdogTick()
{
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Soft/PreferCache should land well under 1s when warm; LQIP stuck longer
    // than this forces ensure + install. Was 2500ms — felt "frozen on LQIP".
    constexpr qint64 kStuckMs = 900;
    bool needWindow = false;
    int repaired = 0;
    // Only on-screen (+small overscan) — never walk hundreds of off-screen tiles
    // on the GUI thread while the user is scrolling.
    const QRectF sceneVisible = mapToScene(viewport()->rect().adjusted(-80, -80, 80, 80))
                                    .boundingRect();

    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QRectF tile = item->contentSceneRect();
        if (!tile.isNull() && tile.isValid() && !tile.intersects(sceneVisible)) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        GallerySoftState &st = m_gallerySoft[path];
        // Terminal soft path: tiles/host own display — never force ensure again.
        if (st.terminal || st.failed
            || st.ensureAttempts >= GallerySoft::kMaxEnsureAttempts) {
            st.terminal = true;
            continue;
        }
        // Durable pyramid: soft underlay is optional; watchdog must not storm.
        if (ThumtooCache::hasDurableTilesKnown(path)
            && item->displayPixelLongEdge() > DisplayQuality::kLqipMaxEdge) {
            st.terminal = true;
            continue;
        }

        // Install policy via bound surface + controller evaluate.
        int target = st.want > 0 ? st.want
            : galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        {
            const QSize logical = logicalSizeForPath(path);
            const int native = qMax(logical.width(), logical.height());
            if (native > 0) {
                target = qMin(target, native);
            }
        }
        const bool climbPending =
            st.inflight > 0
            || (m_pathRaster && m_pathRaster->isClimbPending(path));
        const int hostEdge = DisplayQuality::hostLongEdge(path);
        syncItemDisplaySurface(item, hostEdge, climbPending);
        // Gallery soft tick may need a higher need than viewport-derived state.
        if (item->displaySurfaceId() != 0) {
            const auto sid = static_cast<DisplaySurface::SurfaceId>(
                item->displaySurfaceId());
            m_displaySurfaces.setNeed(sid, target);
        }
        const DisplaySurface::Action act =
            (item->displaySurfaceId() != 0)
                ? m_displaySurfaces.evaluate(
                      static_cast<DisplaySurface::SurfaceId>(
                          item->displaySurfaceId()))
                : DisplaySurface::decide(
                      displaySurfaceStateForItem(item, hostEdge, climbPending));
        using AT = DisplaySurface::ActionType;
        if (act.type == AT::None) {
            // Shown only. Host soft while tile is LQIP must not mark st.have done.
            st.have = qMax(st.have, item->displayPixelLongEdge());
            const int shown = item->displayPixelLongEdge();
            if (shown > DisplayQuality::kLqipMaxEdge
                || hostEdge <= shown) {
                st.weakSinceMs = 0;
            }
            // else: leave weakSinceMs so LQIP→soft watchdog can force ensure
        } else {
            // Durable tiles / tile LOD: never Full native from Gallery soft tick.
            const auto pol =
                (target > ThumtooCache::kBatchOverviewEdge
                 && !ThumtooCache::hasDurableTilesKnown(path)
                 && !(item && item->tileLodWanted()))
                    ? PathRasterService::ClimbPolicy::EscalateToFull
                    : PathRasterService::ClimbPolicy::SoftDisplay;
            if (act.type == AT::ScheduleClimb) {
                clearGallerySoftInflight(st);
            }
            const bool sizeChanged =
                applyDisplaySurfaceAction(item, act, QImage(), target, pol);
            if (act.type == AT::AttachSoft || act.type == AT::AttachFull) {
                ++repaired;
                Q_UNUSED(sizeChanged);
            }
            if (act.type == AT::ScheduleClimb) {
                scheduleGalleryDecode(path);
                if (m_pathRaster && m_pathRaster->isClimbPending(path)) {
                    needWindow = true;
                }
            }
            st.have = qMax(st.have, item->displayPixelLongEdge());
            if (item->displayPixelLongEdge() > DisplayQuality::kLqipMaxEdge) {
                st.weakSinceMs = 0;
            }
        }

        // Tile cells: LQIP underlay is intentional until tiles cover — not stuck soft.
        if (item->tileLodWanted()) {
            st.terminal = true;
            st.weakSinceMs = 0;
            continue;
        }

        // Blank / LQIP for long enough with no climb: force one ensure cycle
        // only for non-tile (tiny) cells.
        if (!item->hasDisplayPixels()
            || (item->displayPixelLongEdge() > 0
                && item->displayPixelLongEdge() <= DisplayQuality::kLqipMaxEdge
                && target > DisplayQuality::kLqipMaxEdge
                && !climbPending
                && act.type == AT::None)) {
            if (st.weakSinceMs <= 0) {
                st.weakSinceMs = now;
            } else if ((now - st.weakSinceMs) > kStuckMs) {
                if (st.terminal || st.ensureAttempts >= GallerySoft::kMaxEnsureAttempts) {
                    st.terminal = true;
                    st.weakSinceMs = 0;
                } else {
                    clearGallerySoftInflight(st);
                    // Do not clear gaveUpWant forever — limited retries via ensureAttempts.
                    scheduleGalleryDecode(path);
                    needWindow = true;
                    st.weakSinceMs = 0;
                }
            }
        } else if (item->hasDisplayPixels()
                   && item->displayPixelLongEdge() > DisplayQuality::kLqipMaxEdge) {
            st.weakSinceMs = 0;
        }

        if (item->hasDisplayPixels()) {
            const int edge = item->displayPixelLongEdge();
            if (edge > st.have) {
                st.have = edge;
            }
        }

        // Stuck inflight: clear and allow reschedule (do not leave forever).
        if (st.inflight > 0 && st.inflightSinceMs > 0
            && (now - st.inflightSinceMs) > kStuckMs) {
            if (const char *dbg = std::getenv("THUMTOO_DEBUG");
                dbg && dbg[0] && dbg[0] != '0') {
                fprintf(stderr,
                        "biltoo/gallery: soft STUCK path need=%d have=%d inflight=%d age=%lldms — reset\n",
                        st.want, st.have, st.inflight,
                        static_cast<long long>(now - st.inflightSinceMs));
            }
            st.inflight = 0;
            st.inflightSinceMs = 0;
            needWindow = true;
        }

    }

    if (repaired > 0 && viewport()) {
        viewport()->update();
    }
    if (needWindow) {
        updateGalleryDecodeWindow();
    }
    updateGallerySoftProgressHud();
}
