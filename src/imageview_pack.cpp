// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayquality.h"
#include "imageview.h"
#include "layoutapplyguard.h"
#include "gallerypackfit.h"
#include "viewtransform.h"
#include <cstdio>
#include <cstdlib>
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
    m_galleryStatusRefreshTimer->setInterval(ViewTransform::nonNegMs(delayMs));
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
    m_galleryDecodeScrollTimer->setInterval(ViewTransform::nonNegMs(delayMs));
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
        // LQIP only from ImageCache (warmSessionOpenMemos / size-probe workers).
        // Never ThumtooCache::cachedLqipImage on the GUI — it is a no-op there.
        QImage hostSample = ImageCache::get(path);
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
        GallerySoftState &st = m_gallerySoftBook.soft[path];
        // Shown edge only — hostEdge can exceed what install actually attached.
        st.have = GallerySoft::maxHave(st.have, after);
        item->update();
        ++installed;
    }
    return installed;
}

void ImageView::publishGalleryInterest(const QStringList &interestNear,
                                       const QStringList &interestRest)
{
    // Gallery drives tiles via TileLoadCoordinator + scheduleTilePyramid only
    // when durable coverage is missing. setInterest Primary used to enqueue
    // FocusFull pyramids (and soft PreferCache) every decode window — that
    // reintroduced multi-second worker load after we stopped unconditional
    // scheduleTilePyramid. Interest is unused for Gallery.
    Q_UNUSED(interestNear);
    Q_UNUSED(interestRest);
}

void ImageView::scheduleIdleGalleryDecodes(const QStringList &rest)
{
    // Soft PreferCache is removed from Gallery. Off-screen work is tiles via
    // the coordinator when cells enter the viewport — no idle soft climb.
    Q_UNUSED(rest);
}


GalleryLayout::Mode ImageView::galleryLayoutModeFromViewMode() const
{
    switch (m_layout.mode) {
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
    if (m_perf.enabled) {
        decodeWinTimer.start();
    }
    // -------------------------------------------------------------------------
    // Gallery decode window (viewport inspection)
    //
    // Pass 1 — ImageCache LQIP onto blank cells (budgeted).
    // Pass 2 — scheduleGalleryDecode for blanks (LQIP install + tile pyramid if
    //          durable coverage missing). Tiles issued by TileLoadCoordinator.
    // Soft PreferCache is not used.
    // -------------------------------------------------------------------------
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }
    // While sizes are still sequential, only allow blank LQIP installs from cache
    // — no tile ticks / pyramid (workers stay on ProbeSize).
    if (gallerySizeResolveActive()) {
        constexpr int kMaxInstallsDuringSizeResolve = GallerySoft::kMaxInstallsDuringSizeResolve;
        bool more = false;
        const int n = galleryInstallHostSoftOntoBlanks(kMaxInstallsDuringSizeResolve, &more);
        if (n > 0 && viewport()) {
            viewport()->update();
        }
        if (more) {
            scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowRearmMs);
        }
        return;
    }
    // Wall budget: cold open was stacking install + schedule + tile tick past
    // GUI_BUDGET. Slice work and re-arm instead of one multi-hundred-ms pass.
    QElapsedTimer wall;
    wall.start();
    constexpr qint64 kDecodeWindowWallMs = GallerySoft::kDecodeWindowWallMs;

    const QRect viewRect = viewport()->rect().adjusted(
        -GalleryPackFit::kDecodeOverscanPx, -GalleryPackFit::kDecodeOverscanPx,
        GalleryPackFit::kDecodeOverscanPx, GalleryPackFit::kDecodeOverscanPx);
    const QRectF sceneVisible = mapToScene(viewRect).boundingRect();

    qint64 usPass1 = 0;
    qint64 usPass2 = 0;
    qint64 usInterest = 0;
    QElapsedTimer phaseTimer;

    constexpr int kMaxInstallsPerDecodeWindow = GallerySoft::kMaxInstallsPerDecodeWindow;
    bool moreInstallsPending = false;
    if (m_perf.enabled) {
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
        scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowRearmMs);
    }
    if (m_perf.enabled) {
        usPass1 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    // ------------------------------------------------------------------
    // Pass 2: LQIP install for blank cells (tiles owned by coordinator).
    // ------------------------------------------------------------------
    QStringList visible;
    QSet<QString> seen;

    // Prefer viewport hits only — full m_items + tileLodWanted was O(n) and
    // dominated cold decode windows on large sessions.
    const QList<QGraphicsItem *> hit =
        sceneVisible.isNull()
            ? QList<QGraphicsItem *>()
            : scene()->items(sceneVisible, Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem *gi : hit) {
        if (wall.elapsed() >= kDecodeWindowWallMs) {
            scheduleGalleryDecodeWindowRefresh(16);
            break;
        }
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item) {
            continue;
        }
        const QString &path = item->path();
        if (path.isEmpty() || seen.contains(path)) {
            continue;
        }
        seen.insert(path);

        GallerySoftState &st = m_gallerySoftBook.soft[path];
        st.have = GallerySoft::maxHave(st.have, item->displayPixelLongEdge());
        st.terminal = true;

        // Blank on-screen cells only — off-screen waits until scrolled in.
        if (!item->hasDisplayPixels()) {
            visible.append(path);
        }
    }

    constexpr int kSchedBudget = 32;
    int scheduled = 0;
    for (const QString &path : visible) {
        if (scheduled >= kSchedBudget || wall.elapsed() >= kDecodeWindowWallMs) {
            scheduleGalleryDecodeWindowRefresh(16);
            break;
        }
        scheduleGalleryDecode(path);
        ++scheduled;
    }
    if (m_perf.enabled) {
        usPass2 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    const bool lqipBusy = scheduled > 0 || moreInstallsPending;
    if (m_perf.enabled) {
        usInterest = phaseTimer.nsecsElapsed() / 1000;
    }

    // Tile issue: one coordinator tick per decode window (not per path).
    // Skip if the LQIP/schedule slice already burned the wall — re-arm instead.
    if (wall.elapsed() < kDecodeWindowWallMs) {
        int tileBudget = isGalleryMode() ? 32 : 8;
        tickPrimaryTileLod(tileBudget);
    } else {
        scheduleGalleryDecodeWindowRefresh(16);
    }

    // Rate-limited tile debug (BILTOO_TILE_DEBUG=1) — sample viewport hits only.
    if (const char *td = std::getenv("BILTOO_TILE_DEBUG");
        td && td[0] && td[0] != '0') {
        static qint64 s_lastLogMs = 0;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - s_lastLogMs >= 500) {
            s_lastLogMs = now;
            std::fprintf(stderr,
                         "biltoo/tile: lqipBusy=%d visibleSched=%d\n",
                         lqipBusy ? 1 : 0, scheduled);
            std::fflush(stderr);
        }
    }

    // Re-arm while LQIP installs or schedules remain; tile coverage continues
    // via TileLoadCoordinator re-arm / completion wake.
    if (scheduled > 0 || moreInstallsPending) {
        scheduleGalleryDecodeWindowRefresh(16);
    }
    updateGallerySoftProgressHud();
    if (m_perf.enabled && decodeWinTimer.isValid()) {
        m_perf.noteDecodeWindowUs(decodeWinTimer.nsecsElapsed() / 1000);
        if (m_perf.lastDecodeWindowUs > 4000) {
            fprintf(stderr,
                    "biltoo/perf: updateGalleryDecodeWindow %.1f ms "
                    "(max %.1f ms runs=%d items=%d "
                    "pass1=%.1f pass2=%.1f interest=%.1f install=%d)\n",
                    m_perf.lastDecodeWindowUs / 1000.0,
                    m_perf.maxDecodeWindowUs / 1000.0, m_perf.decodeWindowRuns,
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
        if (m_layout.mode != LayoutMode::FreeForm) {
            // Should not happen in Workspace (always FreeForm).
        }
        m_layout.mode = LayoutMode::FreeForm;
        for (ImageItem *item : m_items) {
            applyItemModeFlags(item);
        }
        restoreFreeFormStates();
        if (!m_items.isEmpty()) {
            m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-64, -64, 64, 64));
        }
        m_framing.fitMode = false;
        emit statusChanged();
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!isGalleryMode()) {
        enterGallery(mode);
        return;
    }

    if (m_layout.mode == LayoutMode::FreeForm && mode != LayoutMode::FreeForm) {
        snapshotFreeFormStates();
    }

    m_layout.mode = mode;
    for (ImageItem *item : m_items) {
        applyItemModeFlags(item);
    }
    applyLayout(GalleryPackReason::EnterGallery);
}

void ImageView::setGridColumns(int columns)
{
    const int before = m_layout.gridColumns;
    m_layout.setGridColumns(columns);
    if (m_layout.gridColumns == before) {
        return;
    }
    if (isGalleryMode()
        && (m_layout.mode == LayoutMode::Grid || m_layout.mode == LayoutMode::GridCrop
            || m_layout.mode == LayoutMode::Flow || m_layout.mode == LayoutMode::FlowFill
            || m_layout.mode == LayoutMode::Facing)) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryColumns(int columns)
{
    const int before = m_layout.masonryColumns;
    m_layout.setMasonryColumns(columns);
    if (m_layout.masonryColumns == before) {
        return;
    }
    if ((m_layout.mode == LayoutMode::Masonry || m_layout.mode == LayoutMode::MasonryFill)
        && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setMasonryRows(int rows)
{
    const int before = m_layout.masonryRows;
    m_layout.setMasonryRows(rows);
    if (m_layout.masonryRows == before) {
        return;
    }
    if ((m_layout.mode == LayoutMode::MasonryRows || m_layout.mode == LayoutMode::MasonryRowsFill)
        && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void ImageView::setGalleryRelayoutSuppressed(bool on)
{
    if (on) {
        m_galleryRelayoutSuppress.push(true);
        if (m_layoutDebounceTimer) {
            m_layoutDebounceTimer->stop();
        }
    } else if (m_galleryRelayoutSuppress.active()) {
        m_galleryRelayoutSuppress.push(false);
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
        m_bindBook.binds.append(b);
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
    if (m_layoutApply.active()) {
        return;
    }
    // Size-first open: do not pack on provisional stand-ins while probes run.
    if (gallerySizeResolveActive()) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    if (!isGalleryMode() || m_items.isEmpty() || m_layout.mode == LayoutMode::FreeForm) {
        return;
    }

    if (!m_pathOrderBook.paths.isEmpty()) {
        reorderItemsByPaths(m_pathOrderBook.paths);
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

    LayoutApplyGuard::Scoped layoutApplyScope(&m_layoutApply);

    // Packaged layouts use view pixels as scene units so images scale to the window
    resetTransform();
    if (!m_gallery.pendingRestore() && !preserveView) {
        centerOn(0, 0);
    }

    // Reserve scrollbar space for the pack measurement. AsNeeded would let the
    // first bar appear, shrink the viewport, and leave the fitted axis slightly
    // oversized (dual bars). AlwaysOn only for this critical section; policy is
    // restored after sceneRect is set so Zoom Fit/Fill can hide unused bars.
    // m_layoutApply is already active — resizeEvent will not re-enter pack.
    const auto savedHBar = horizontalScrollBarPolicy();
    const auto savedVBar = verticalScrollBarPolicy();
    if (savedHBar != Qt::ScrollBarAlwaysOn || savedVBar != Qt::ScrollBarAlwaysOn) {
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    const qreal margin = GalleryLayout::Params::kDefaultMargin;
    const qreal gap = GalleryLayout::Params::kDefaultGap;
    const qreal availW = GalleryPackFit::packAvailAxis(viewport()->width(), margin);
    const qreal availH = GalleryPackFit::packAvailAxis(viewport()->height(), margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_layout.masonryColumns;
    params.gridColumns = m_layout.gridColumns;
    params.masonryRows = m_layout.masonryRows;
    params.mode = galleryLayoutModeFromViewMode();

    GalleryLayout::pack(m_items, params, [this](ImageItem *item) {
        m_itemStateBook.byPath.insert(item->path(), captureState(item));
    });

    const QRectF bounds = ViewTransform::padded(m_scene->itemsBoundingRect(), margin);
    if (m_scene->sceneRect() != bounds) {
        m_scene->setSceneRect(bounds);
    }
    // Restore caller policy (AsNeeded/Off). With overshoot correction the packed
    // fitted axis should not need a bar; AsNeeded can hide it. Still under
    // m_layoutApply so a policy-driven resize does not repack.
    if (horizontalScrollBarPolicy() != savedHBar) {
        setHorizontalScrollBarPolicy(savedHBar);
    }
    if (verticalScrollBarPolicy() != savedVBar) {
        setVerticalScrollBarPolicy(savedVBar);
    }
    m_framing.fitMode = true;
    // Keep the guard until after statusChanged so slots cannot re-enter layout.
    emit statusChanged();
    // layoutApplyScope ends after this function returns (keeps guard through statusChanged)
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
    params.availW = GalleryPackFit::packAvailAxis(viewport()->width(), margin);
    params.availH = GalleryPackFit::packAvailAxis(viewport()->height(), margin);

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
        m_itemStateBook.byPath.insert(item->path(), captureState(item));
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
    if (m_centreProgress.matchesTitlePrefix(tr("Improving previews"))) {
        clearCentreProgress();
    }
}

void ImageView::gallerySoftWatchdogTick()
{
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }
    // Soft PreferCache is gone. Watchdog only re-installs LQIP on blank
    // on-screen cells and keeps the tile coordinator awake.
    const QRectF sceneVisible =
        mapToScene(viewport()->rect().adjusted(-80, -80, 80, 80)).boundingRect();
    bool needWindow = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const QRectF tile = item->contentSceneRect();
        if (!tile.isNull() && tile.isValid() && !tile.intersects(sceneVisible)) {
            continue;
        }
        if (!item->hasDisplayPixels()) {
            scheduleGalleryDecode(item->path());
            needWindow = true;
        }
    }
    if (needWindow) {
        updateGalleryDecodeWindow();
    } else {
        tickPrimaryTileLod(48);
    }
    updateGallerySoftProgressHud();
}
