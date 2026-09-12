// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "thumtoocache.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QDateTime>
#include <cstdio>
#include <QUndoStack>

void ImageView::updateGalleryDecodeWindow()
{
    // -------------------------------------------------------------------------
    // Gallery soft-thumb algorithm (viewport inspection)
    //
    // Zoom: Ctrl+wheel / toolbar scales the *view transform*. Pack cell size in
    // scene space is unchanged; on-screen pixel size grows with zoom × DPR.
    //
    // Per path (GallerySoftState):
    //   1. Placeholder — always want at least kFilmstripLadderEdge (256) so a
    //      tile is never blank once soft data exists; idle tiles stay in the
    //      soft band (≤ kGalleryLadderEdge).
    //   2. want — for a *visible* tile: ceilLadder(on-screen long edge × DPR).
    //      No artificial soft-max cliff; no native full dump. Off-screen:
    //      placeholder band only.
    //   3. If have >= want → idle (show whatever we have; sharper wins).
    //   4. If inflight → wait (exactly one request per path).
    //   5. If failed → stop.
    //   6. If gaveUpWant >= want && have > 0 → stop (will not grow further).
    //   7. Soft first: min(want, 512) via schedulePixels / request_raster SoftOnly.
    //      Higher edges: setInterest overview (or scheduleOverviewPixels without
    //      SET_INTEREST). Never ImageLoader::load for Gallery.
    //
    // Image mode still does full native decode separately.
    // -------------------------------------------------------------------------
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }

    const QRect viewRect = viewport()->rect().adjusted(
        -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
        kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
    const QRectF sceneVisible = mapToScene(viewRect).boundingRect();

    QStringList visible;
    QStringList rest;
    QStringList interestNear;
    QStringList interestRest;
    QSet<QString> seen;

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
        if (tile.isNull() || !tile.isValid()) {
            continue;
        }
        const bool onScreen = tile.intersects(sceneVisible);
        if (onScreen) {
            interestNear.append(path);
        } else {
            interestRest.append(path);
        }

        GallerySoftState &st = m_gallerySoft[path];
        if (st.failed || st.inflight > 0) {
            continue;
        }

        st.have = item->displayPixelLongEdge();
        if (item->hasDecodedPixels()) {
            st.have = qMax(st.have, item->displayPixelLongEdge());
            continue;
        }

        const int want = galleryWantEdgeForPath(path, sceneVisible);
        st.want = want;
        if (st.have >= want) {
            continue;
        }
        if (st.gaveUpWant >= want && st.have > 0) {
            continue;
        }

        if (onScreen) {
            visible.append(path);
        } else {
            rest.append(path);
        }
    }

    // Gallery interest: soft-band only (no Primary FocusFull). Overview climb
    // past soft is explicit scheduleOverviewPixels with host ladderReady —
    // setInterest at batch edge starved SoftOnly and delayed first tiles.
    {
        const int softEdge = ThumtooCache::kGalleryLadderEdge;
        QStringList near = interestNear;
        near.sort();
        QStringList speculative = interestRest;
        speculative.sort();
        (void)ThumtooCache::setInterest(near, speculative, softEdge, softEdge,
                                        /*pathsPrimary=*/{}, /*primaryEdge=*/0);
    }

    for (const QString &path : visible) {
        scheduleGalleryDecode(path);
    }

    const int freeSlots =
        galleryDecodeConcurrency() - gallerySoftInflightCount();
    if (freeSlots > 0 && !rest.isEmpty()) {
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
    if (m_applyingLayout) {
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
    QPointF keptCenter;
    if (preserveView && viewport()) {
        keptCenter = mapToScene(viewport()->rect().center());
    }

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
    switch (m_layoutMode) {
    case LayoutMode::SideBySide:
        params.mode = GalleryLayout::Mode::SideBySide;
        break;
    case LayoutMode::Vertical:
        params.mode = GalleryLayout::Mode::Vertical;
        break;
    case LayoutMode::Grid:
        params.mode = GalleryLayout::Mode::Grid;
        break;
    case LayoutMode::GridCrop:
        params.mode = GalleryLayout::Mode::GridCrop;
        break;
    case LayoutMode::Masonry:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    case LayoutMode::MasonryRows:
        params.mode = GalleryLayout::Mode::MasonryRows;
        break;
    case LayoutMode::MasonryFill:
        params.mode = GalleryLayout::Mode::MasonryFill;
        break;
    case LayoutMode::MasonryRowsFill:
        params.mode = GalleryLayout::Mode::MasonryRowsFill;
        break;
    case LayoutMode::Flow:
        params.mode = GalleryLayout::Mode::Flow;
        break;
    case LayoutMode::FlowFill:
        params.mode = GalleryLayout::Mode::FlowFill;
        break;
    case LayoutMode::Facing:
        params.mode = GalleryLayout::Mode::Facing;
        break;
    default:
        params.mode = GalleryLayout::Mode::Masonry;
        break;
    }

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
        // Scene geometry changed; map the previous centre back if it still
        // falls in the new bounds (clamped by QGraphicsView otherwise).
        centerOn(keptCenter);
    }
    updateGalleryDecodeWindow();
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

void ImageView::gallerySoftWatchdogTick()
{
    if (!isGalleryMode() || m_items.isEmpty()) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    constexpr qint64 kStuckMs = 8000;
    bool needWindow = false;
    int repaired = 0;

    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        GallerySoftState &st = m_gallerySoft[path];

        // Cache/session soft exists but tile still empty → install + force paint.
        if (!item->hasDisplayPixels()) {
            QImage soft = m_previewByPath.value(path);
            if (soft.isNull()) {
                soft = ImageCache::get(path);
            }
            // Do not PreferCache / extract on the GUI thread here.
            if (!soft.isNull()) {
                installDisplayPixels(item, soft, SessionAppearance::PixelKind::SoftPreview,
                                     item->sessionId());
                if (m_scene) {
                    m_scene->update(item->sceneBoundingRect());
                }
                st.have = qMax(st.have, qMax(soft.width(), soft.height()));
                ++repaired;
                continue;
            }
        } else {
            // State says no soft but item has pixels — keep have in sync.
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

#ifndef NDEBUG
        // Debug builds: never leave "have soft in cache, blank tile" silent.
        if (st.have > 0 && !item->hasDisplayPixels()) {
            Q_ASSERT_X(false, "gallerySoftWatchdogTick",
                       "Gallery soft have>0 but item has no display pixels");
        }
#endif
    }

    if (repaired > 0 && viewport()) {
        viewport()->update();
    }
    if (needWindow) {
        updateGalleryDecodeWindow();
    }
}
