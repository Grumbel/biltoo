// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerycontroller.h"
#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"

#include <QScrollBar>
#include <QTimer>
#include <QSet>

GalleryController::GalleryController(ImageView *view)
    : m_view(view)
{
}

void GalleryController::discardStash()
{
    // Take ownership first so re-entrant callers (and double-discard) see an
    // empty list. Duplicates in the list would otherwise double-free.
    QList<ImageItem *> doomed = m_stashedItems;
    m_stashedItems.clear();
    m_stashedPathOrder.clear();
    QSet<ImageItem *> seen;
    for (ImageItem *item : doomed) {
        if (!item || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        if (QGraphicsScene *sc = item->scene()) {
            sc->removeItem(item);
        }
        delete item;
    }
}

void GalleryController::stashItems()
{
    // Replace any previous stash (should be empty when leaving Gallery).
    discardStash();
    if (m_view->liveItems().isEmpty()) {
        return;
    }
    m_stashedPathOrder = m_view->pathOrder();
    m_stashedItems = m_view->liveItems();
    m_selectionAnchor = nullptr;
    m_hoverPath.clear();
    for (ImageItem *item : m_stashedItems) {
        if (!item) {
            continue;
        }
        item->setSelected(false);
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
    }
    m_view->liveItems().clear();
    // Keep gallery decode scheduled / failed on the view — paths still valid
    // on return. Pending LoadAdd for missing tiles can finish after restore.
}

void GalleryController::restoreStashedItems()
{
    if (m_stashedItems.isEmpty()) {
        return;
    }
    // Live canvas should be empty (Image mode held a single item that
    // clearWorkspace removes before Gallery is entered).
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->scene()) {
            item->scene()->removeItem(item);
        }
        delete item;
    }
    m_view->liveItems() = m_stashedItems;
    m_stashedItems.clear();
    if (!m_stashedPathOrder.isEmpty()) {
        m_view->pathOrder() = m_stashedPathOrder;
    }
    m_stashedPathOrder.clear();
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        if (!item->scene()) {
            m_view->canvasScene()->addItem(item);
        }
        m_view->applyItemModeFlags(item);
        // Image-mode crop updates the appearance store and may have synced a
        // bake onto the stash; if soft still lags (or was never baked), force
        // rematerialize from the store so Gallery does not show full-frame.
        m_view->rematerializeGalleryItemFromStore(item);
    }
    m_view->reorderItemsByPaths(m_view->pathOrder());
}

void GalleryController::snapshotViewport()
{
    if (!m_view->isGalleryMode()) {
        return;
    }
    // Scene centre is robust across ScrollBar AlwaysOn/Off and pack rebuilds;
    // raw scrollbar values are not (policy change zeroes the range).
    m_viewCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    m_haveViewCenter = true;
    if (m_view->horizontalScrollBar()) {
        m_scrollH = m_view->horizontalScrollBar()->value();
    }
    if (m_view->verticalScrollBar()) {
        m_scrollV = m_view->verticalScrollBar()->value();
    }
    m_haveScroll = true;
    if (ImageItem *sel = m_view->targetItem()) {
        m_focusPath = sel->path();
    }
}

void GalleryController::restoreViewport(const QString &focusPath)
{
    if (!focusPath.isEmpty()) {
        m_focusPath = focusPath;
    }
    m_pendingRestore = true;
    // Try immediately if already in Gallery with items; otherwise applyLayout
    // will re-apply after each pack while pending stays true.
    if (m_view->isGalleryMode()) {
        applyPendingRestore();
    }
}

void GalleryController::applyPendingRestore()
{
    if (!m_pendingRestore || !m_view->isGalleryMode()) {
        return;
    }

    reassertViewport();

    ImageItem *focus = nullptr;
    if (!m_focusPath.isEmpty()) {
        focus = m_view->findItemByPath(m_focusPath);
    }
    if (focus) {
        focus->setSelected(true);
        // With a scroll snapshot, do not ensureVisible — that recentres and
        // undoes the restored scroll (felt like “Gallery jumps after crop”).
        if (!m_haveScroll) {
            const QRectF viewScene =
                m_view->mapToScene(m_view->viewport()->rect()).boundingRect();
            if (!viewScene.intersects(focus->sceneBoundingRect())) {
                m_view->ensureVisible(focus, 48, 48);
            }
        }
        if (m_hoverPath != focus->path()) {
            m_hoverPath = focus->path();
            m_view->viewport()->update();
        }
    }

    // Stay pending while loads complete — each applyLayout would otherwise
    // centerOn(0,0) and wipe the restored position.
    if (!m_view->hasPendingWorkspacePaths() && !m_view->liveItems().isEmpty()) {
        m_pendingRestore = false;
    }
}

void GalleryController::reassertViewport()
{
    if (!m_view->isGalleryMode()) {
        return;
    }
    // Prefer scrollbar pixels. Scene centre from leave-for-Image is invalid
    // after restash/repack (crop aspect change, ContentChange pack) and was
    // jumping the overview when returning after a crop.
    if (m_haveScroll) {
        if (m_view->horizontalScrollBar()) {
            m_view->horizontalScrollBar()->setValue(m_scrollH);
        }
        if (m_view->verticalScrollBar()) {
            m_view->verticalScrollBar()->setValue(m_scrollV);
        }
        return;
    }
    if (m_haveViewCenter) {
        m_view->centerOn(m_viewCenter);
    }
}


void GalleryController::onLeave(int nextMode)
{
    const auto next = static_cast<ImageView::ViewMode>(nextMode);
    m_hoverPath.clear();
    m_selectionAnchor = nullptr;
    m_view->setDragMode(QGraphicsView::NoDrag);
    // Stop deferred packs immediately — a pending 0ms debounce after
    // scrollbar/thumb resize must not re-enter applyLayout while we tear down.
    m_view->stopDeferredPacking();
    m_view->cancelGallerySizeResolve();
    m_pendingRestore = false;
    // Gallery → Image: keep scroll/centre snapshot from snapshotViewport()
    // (called just before setViewMode) so return-to-Gallery can restore it.
    // Any other leave path drops the snapshot.
    if (next == ImageView::ViewMode::Image) {
        // Keep tiles + decoded pixels for a fast return to Gallery.
        // Pending LoadAdds may still fill stashed placeholders in Image mode.
        stashItems();
    } else {
        m_haveScroll = false;
        m_haveViewCenter = false;
        // Gallery → Workspace / leave: cancel decode-window jobs AND remove
        // packed live tiles. Leaving them caused drops to "move" grid tiles
        // (same SessionImageId) or keep gallery scale/cell size on the
        // free-form canvas.
        m_view->invalidateGalleryDecodes();
        m_view->clearLiveCanvas();
        m_view->pathOrder().clear();
        m_view->sessionIdOrder().clear();
    }
}

void GalleryController::leaveForImageMode()
{
    // Snapshot must run while still in Gallery (scrollbars + scene centre valid).
    if (m_view->isGalleryMode()) {
        snapshotViewport();
    }
    // Workspace: setViewMode snapshots + stashes free-form tiles.
    m_view->setViewMode(ImageView::ViewMode::Image);
}

void GalleryController::returnFromImage(int layoutMode, const QString &focusPath)
{
    // Arm restore before enter/applyLayout so packs re-centre on the
    // snapshotted scene point (flags preserved across Gallery→Image leave).
    restoreViewport(focusPath);
    enter(layoutMode);
    applyPendingRestore();
}

void GalleryController::enter(int packagedLayoutInt)
{
    auto packagedLayout = static_cast<ImageView::LayoutMode>(packagedLayoutInt);
    if (packagedLayout == ImageView::LayoutMode::FreeForm) {
        packagedLayout = ImageView::LayoutMode::Masonry;
    }
    // Preserve multi-select when only switching Gallery layout (not entering
    // from Image/Workspace — prepareGalleryCanvas clears selection).
    QStringList selectedPaths;
    QString anchorPath;
    const bool layoutSwitch = m_view->isGalleryMode();

    // Returning from Image: reattach cached tiles before packing.
    // Hold paints until after applyLayout so crop-sized cells never show with
    // pre-crop pixels for a frame (peer sync + pack ordering).
    const bool restoredStash = !layoutSwitch && !m_stashedItems.isEmpty();
    const bool holdPaint = restoredStash;
    if (holdPaint && m_view->viewport()) {
        m_view->viewport()->setUpdatesEnabled(false);
    }
    if (restoredStash) {
        restoreStashedItems();
    }

    if (layoutSwitch && m_view->canvasScene()) {
        // Only trust items we still own — selection can briefly hold stale
        // pointers after session deletes that skipped destroyCanvasItem.
        for (QGraphicsItem *gi : m_view->canvasScene()->selectedItems()) {
            auto *ii = qgraphicsitem_cast<ImageItem *>(gi);
            if (!ii || !m_view->liveItems().contains(ii) || ii->scene() != m_view->canvasScene()) {
                continue;
            }
            selectedPaths.append(ii->path());
        }
        if (m_selectionAnchor
            && m_view->liveItems().contains(m_selectionAnchor)
            && m_selectionAnchor->scene() == m_view->canvasScene()) {
            anchorPath = m_selectionAnchor->path();
        } else {
            m_selectionAnchor = nullptr;
        }
    }

    if (m_view->isWorkspaceMode()) {
        m_view->snapshotFreeFormStates();
        m_view->snapshotWorkspace();
    }
    // Leaving Workspace/Image for Gallery: drop workspace stash (layout uses
    // live m_items or rebuilds from session paths).
    m_view->discardStashedWorkspace();
    if (layoutSwitch) {
        // Soft reset: keep items and selection paths; only clear view zoom.
        // Drop scroll snapshot — user asked for a new layout, not return-from-Image.
        m_haveScroll = false;
        m_haveViewCenter = false;
        m_pendingRestore = false;
        m_view->resetTransform();
        m_view->enableFitMode();  // layout-switch soft reset
    } else {
        // Clear residual Image/Workspace view state. Drop previous-mode tiles
        // when there is no Gallery stash to restore — otherwise the Image
        // single-item (or free-form Workspace poses) remain visible until
        // populateGalleryCanvas rebuilds, and used to be packed into a
        // nonsense layout for a frame (cold open glitch).
        m_view->prepareGalleryCanvas();
        if (!restoredStash) {
            m_view->clearLiveCanvas();
        }
    }
    m_view->setActiveMode(ImageView::ViewMode::Gallery, packagedLayout);
    if (!layoutSwitch) {
        m_selectionAnchor = nullptr;
    }
    m_view->setDragMode(QGraphicsView::RubberBandDrag);
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        m_view->applyItemModeFlags(item);
        item->setItemOpacity(1.0);
        // Entering Gallery from Image/Workspace: upright overview. Switching
        // layout inside Gallery keeps user content transforms on the tiles.
        if (!layoutSwitch) {
            item->setItemRotation(0.0);
            item->setItemShear(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
        }
    }
    // Explicit layout action: pack only live items (drop stale path-order holes).
    if (layoutSwitch) {
        QStringList livePaths;
        livePaths.reserve(m_view->liveItems().size());
        for (ImageItem *item : m_view->liveItems()) {
            if (item) {
                livePaths.append(item->path());
            }
        }
        if (!livePaths.isEmpty()) {
            m_view->pathOrder() = livePaths;
        }
    }
    // Pack now only when tiles already belong to this Gallery session:
    // layout switch inside Gallery, or restash return from Image.
    // Cold enter from Image/Workspace still holds the previous mode's tiles
    // (or a single Image item). Packing those first paints a random/wrong
    // layout until populateGalleryCanvas → setWorkspacePaths rebuilds —
    // worst on cold cache while size-resolve runs. First pack is owned by
    // setWorkspacePaths / finishGallerySizeResolve.
    if (layoutSwitch || restoredStash) {
        m_view->applyLayout(GalleryPackReason::EnterGallery);
    }

    if (holdPaint && m_view->viewport()) {
        m_view->viewport()->setUpdatesEnabled(true);
        m_view->viewport()->update();
    }

    if (layoutSwitch && !selectedPaths.isEmpty()) {
        m_view->canvasScene()->clearSelection();
        for (const QString &path : selectedPaths) {
            if (ImageItem *item = m_view->findItemByPath(path)) {
                item->setSelected(true);
            }
        }
        m_selectionAnchor = anchorPath.isEmpty()
            ? nullptr
            : m_view->findItemByPath(anchorPath);
    }

    emit m_view->statusChanged();
}
