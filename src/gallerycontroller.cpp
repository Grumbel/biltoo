// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerycontroller.h"
#include <memory>
#include <QFileInfo>
#include "thumtoocache.h"
#include "imageview.h"
#include "packorderview.h"
#include "sessionbindbook.h"
#include "imageview_types.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "viewtransform.h"
#include "layoutapplyguard.h"
#include <QElapsedTimer>
#include "sessionappearance.h"
#include "biltoo_logging.h"
#include "biltoo_thread.h"
#include "gallerypackfit.h"
#include "imagecache.h"
#include "displayquality.h"
#include "workspacenavgeometry.h"
#include "gallerysoftsm.h"

#include <QScrollBar>
#include <QTimer>
#include <QObject>
#include <QUndoStack>
#include <QSet>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QGraphicsScene>
#include <algorithm>
#include <QGraphicsItem>

GalleryController::GalleryController(ImageView *view)
    : m_view(view)
{
}

void GalleryController::discardStash()
{
    // Take ownership first so re-entrant callers (and double-discard) see an
    // empty list. Duplicates in the list would otherwise double-free.
    // Not destroyCanvasItem: items are off-list after the take, and discard must
    // not clear the undo stack or write path-book state from abandoned tiles.
    QList<ImageItem *> doomed = m_stashedItems;
    m_stashedItems.clear();
    m_stashedPackOrder = PackOrderView();
    QSet<ImageItem *> seen;
    for (ImageItem *item : doomed) {
        if (!item || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        // Stage 2: bags + display surface before delete (not the scene-clear path).
        m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        m_view->hostDisplayPipeline().releaseTileBag(item);
        m_view->hostDisplayPipeline().unregisterItemDisplaySurface(item);
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
    m_stashedPackOrder = m_view->currentPackOrder();
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
    // Residual live canvas (e.g. Image-mode tile) must go through destroyCanvasItem
    // so tile bags, display surfaces, interact anchors, and list membership stay
    // consistent — same path as WorkspaceController::restoreStashedItems.
    while (!m_view->liveItems().isEmpty()) {
        m_view->destroyCanvasItem(m_view->liveItems().last());
    }
    m_view->liveItems() = m_stashedItems;
    m_stashedItems.clear();
    if (!m_stashedPackOrder.isEmpty()) {
        m_view->setPathOrder(m_stashedPackOrder);
    }
    m_stashedPackOrder = PackOrderView();
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
    {
        const PackOrderView pack = m_view->currentPackOrder();
        m_view->reorderItemsByPaths(pack.paths());
    }
    // Image-mode navigation may have filled global path RAM; bind/paint without
    // waiting for the next decode-window timer.
    m_view->hostDisplayPipeline().tickPrimaryTileLod(16);
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
    if (!m_view->hostDisplayPipeline().loadGate().hasPendingWorkspacePaths() && !m_view->liveItems().isEmpty()) {
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
    clearChrome();
    m_view->setDragMode(QGraphicsView::NoDrag);
    // Stop deferred packs immediately — a pending 0ms debounce after
    // scrollbar/thumb resize must not re-enter applyLayout while we tear down.
    m_view->stopDeferredPacking();
    m_view->hostGallerySizeResolve().cancel();
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
        m_view->hostGallery().invalidateDecodes();
        m_view->clearLiveCanvas();
        m_view->clearPathOrder();
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
    auto packagedLayout = static_cast<LayoutMode>(packagedLayoutInt);
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
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
        m_view->hostWorkspace().snapshotFreeFormStates();
        m_view->hostWorkspace().snapshot();
    }
    // Leaving Workspace/Image for Gallery: drop workspace stash (layout uses
    // live m_items or rebuilds from session paths).
    m_view->hostWorkspace().discardStash();
    if (layoutSwitch) {
        // Soft reset: keep items and selection paths; only clear view zoom.
        // Drop scroll snapshot — user asked for a new layout, not return-from-Image.
        m_haveScroll = false;
        m_haveViewCenter = false;
        m_pendingRestore = false;
        m_view->resetTransform();
        m_view->hostFraming().setFitOnly();  // layout-switch soft reset
    } else {
        // Clear residual Image/Workspace view state. Drop previous-mode tiles
        // when there is no Gallery stash to restore — otherwise the Image
        // single-item (or free-form Workspace poses) remain visible until
        // populateGalleryCanvas rebuilds, and used to be packed into a
        // nonsense layout for a frame (cold open glitch).
        m_view->hostGallery().prepareCanvas();
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
        {
            ItemComponents::Placement pl = item->placement();
            pl.opacity = 1.0;
            // Entering Gallery from Image/Workspace: upright overview. Switching
            // layout inside Gallery keeps user content transforms on the tiles.
            if (!layoutSwitch) {
                pl.rotation = 0.0;
                pl.shear = 0.0;
                pl.hFlip = false;
                pl.vFlip = false;
            }
            item->applyPlacement(pl);
        }
    }
    // Explicit layout action: pack only live items (drop stale path-order holes).
    if (layoutSwitch) {
        // Drop stale pack holes; keep path∥sessionId from live tiles (id-safe).
        m_view->setPathOrderFromLiveItems();
    }
    // Pack now only when tiles already belong to this Gallery session:
    // layout switch inside Gallery, or restash return from Image.
    // Cold enter from Image/Workspace still holds the previous mode's tiles
    // (or a single Image item). Packing those first paints a random/wrong
    // layout until populateGalleryCanvas → setWorkspacePaths rebuilds —
    // worst on cold cache while size-resolve runs. First pack is owned by
    // setWorkspacePaths / finishGallerySizeResolve.
    if (layoutSwitch || restoredStash) {
        applyLayout(GalleryPackReason::EnterGallery);
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

// --- Gallery input (Tier 6c; moved from imageview_input.cpp) ---

void GalleryController::updateGalleryHoverAt(const QPoint &viewPos)
{
    if (!m_view->isGalleryMode() || !m_view->canvasScene()) {
        if (!hoverPath().isEmpty()) {
            clearHoverPath();
            m_view->viewport()->update();
        }
        return;
    }
    QString path;
    const QPointF scenePos = m_view->mapToScene(viewPos);
    for (QGraphicsItem *gi : m_view->canvasScene()->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            path = ii->path();
            break;
        }
    }
    if (path != hoverPath()) {
        setHoverPath(path);
        m_view->viewport()->update();
    }
}

bool GalleryController::tryWheelGalleryZoom(QWheelEvent *event)
{
    // Gallery: Ctrl+wheel zooms the view (inspection) and refreshes the soft
    // ladder for the new on-screen cell size.
    if (!m_view->isGalleryMode() || !(event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const qreal factor = ViewTransform::wheelZoomFactor(event->angleDelta().y());
    m_view->releaseStickyZoom();
    m_view->hostFraming().clearFitFill();
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->scale(factor, factor);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    // Do not run updateGalleryDecodeWindow or FullViewportUpdate here —
    // each wheel notch used to rescan all tiles + setInterest + repaint
    // every high-res soft, freezing the UI while zooming out.
    scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowScrollMs);
    emit m_view->statusChanged();
    event->accept();
    return true;
}

bool GalleryController::tryWheelGalleryScroll(QWheelEvent *event)
{
    if (!m_view->isGalleryMode()) {
        return false;
    }
    QScrollBar *hBar = m_view->horizontalScrollBar();
    QScrollBar *vBar = m_view->verticalScrollBar();
    const bool canH = hBar && hBar->maximum() > hBar->minimum();
    const bool canV = vBar && vBar->maximum() > vBar->minimum();

    int dx = 0;
    int dy = 0;
    if (!event->pixelDelta().isNull()) {
        dx = event->pixelDelta().x();
        dy = event->pixelDelta().y();
    } else {
        // angleDelta is in eighths of a degree; 120 ≈ one notch.
        dx = event->angleDelta().x();
        dy = event->angleDelta().y();
    }

    // Shift+wheel → prefer horizontal (common UI convention).
    if (event->modifiers() & Qt::ShiftModifier) {
        if (dx == 0 && dy != 0) {
            dx = dy;
            dy = 0;
        }
    }

    // Horizontal strip layouts: vertical wheel pans sideways.
    const bool preferHorizontalScroll =
        m_view->hostLayout().currentMode() == LayoutMode::SideBySide
        || m_view->hostLayout().currentMode() == LayoutMode::MasonryRows
        || m_view->hostLayout().currentMode() == LayoutMode::MasonryRowsFill;

    if (preferHorizontalScroll && dx == 0 && dy != 0) {
        dx = dy;
        dy = 0;
    } else if (dx == 0 && dy != 0 && !canV && canH) {
        dx = dy;
        dy = 0;
    } else if (dy == 0 && dx != 0 && !canH && canV) {
        dy = dx;
        dx = 0;
    }

    if (canH && dx != 0) {
        hBar->setValue(hBar->value() - dx);
    }
    if (canV && dy != 0) {
        vBar->setValue(vBar->value() - dy);
    }
    // Accept even at scroll ends so the event does not fall through to zoom.
    event->accept();
    return true;
}

bool GalleryController::tryMousePressGalleryRight(QMouseEvent *event)
{
    // Gallery right-click: do not let QGraphicsView alter selection (that
    // cancels multi-select before the context menu opens). If the click is on
    // an unselected tile, select only that tile; if it is already selected,
    // keep the current multi-select for bulk rotate/flip/delete.
    if (!m_view->isGalleryMode() || event->button() != Qt::RightButton) {
        return false;
    }
    const QPointF scenePos = m_view->mapToScene(event->pos());
    ImageItem *hit = nullptr;
    for (QGraphicsItem *gi : m_view->canvasScene()->items(scenePos)) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            hit = ii;
            break;
        }
    }
    if (hit && !hit->isSelected()) {
        m_view->canvasScene()->clearSelection();
        hit->setSelected(true);
        setSelectionAnchor(hit);
        if (hit->sessionId() != kInvalidSessionImageId) {
            emit m_view->sessionImageFocused(hit->sessionId());
        } else if (!hit->path().isEmpty()) {
            emit m_view->galleryItemFocused(hit->path());
        }
        emit m_view->statusChanged();
    }
    event->accept();
    return true;
}

bool GalleryController::tryMousePressGalleryLeft(QMouseEvent *event)
{
    // Gallery: classic multi-select (click / Ctrl / Shift); open is double-click.
    if (!m_view->isGalleryMode() || event->button() != Qt::LeftButton
        || (event->modifiers() & Qt::AltModifier)) {
        return false;
    }
        const QPointF scenePos = m_view->mapToScene(event->pos());
        ImageItem *hit = nullptr;
        for (QGraphicsItem *gi : m_view->canvasScene()->items(scenePos)) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                hit = ii;
                break;
            }
        }
        // Ctrl (and Meta on platforms where that is the multi-select modifier)
        // toggles membership without clearing the rest of the selection.
        const bool ctrl = event->modifiers()
                          & (Qt::ControlModifier | Qt::MetaModifier);
        const bool shift = event->modifiers() & Qt::ShiftModifier;

        if (hit && shift && selectionAnchor()) {
            // Session-order range from anchor to hit (inclusive).
            int i0 = m_view->liveItems().indexOf(selectionAnchor());
            int i1 = m_view->liveItems().indexOf(hit);
            if (i0 < 0) {
                i0 = i1;
            }
            if (i1 < 0) {
                i1 = i0;
            }
            if (i0 > i1) {
                std::swap(i0, i1);
            }
            m_view->canvasScene()->blockSignals(true);
            m_view->canvasScene()->clearSelection();
            for (int i = i0; i <= i1 && i < m_view->liveItems().size(); ++i) {
                m_view->liveItems().at(i)->setSelected(true);
            }
            m_view->canvasScene()->blockSignals(false);
            // Selection overlay only — no item cache rebuild.
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
            emit m_view->canvasSelectionChanged();
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit m_view->sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit m_view->galleryItemFocused(hit->path());
            }
            event->accept();
            if (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isVisible()) {
                emit m_view->statusChanged();
            }
            return true;
        }

        if (hit && ctrl) {
            // Defensive: soft/late tiles may have lost ItemIsSelectable.
            if (!(hit->flags() & QGraphicsItem::ItemIsSelectable)) {
                hit->setGallerySelectable(true);
            }
            hit->setSelected(!hit->isSelected());
            if (hit->isSelected()) {
                setSelectionAnchor(hit);
            }
            hit->invalidateDeviceCache();
            emit m_view->canvasSelectionChanged();
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit m_view->sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit m_view->galleryItemFocused(hit->path());
            }
            event->accept();
            if (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isVisible()) {
                emit m_view->statusChanged();
            }
            return true;
        }

        if (hit) {
            // One selectionChanged: clear+select under blocked signals so
            // MainWindow does not run navigation/layout work twice per click.
            if (!(hit->flags() & QGraphicsItem::ItemIsSelectable)) {
                hit->setGallerySelectable(true);
            }
            const bool already = hit->isSelected()
                && m_view->canvasScene()->selectedItems().size() == 1;
            if (already) {
                setSelectionAnchor(hit);
                event->accept();
                return true;
            }
            m_view->canvasScene()->blockSignals(true);
            m_view->canvasScene()->clearSelection();
            hit->setSelected(true);
            m_view->canvasScene()->blockSignals(false);
            // Selection overlay in drawForeground; viewport update is enough.
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
            emit m_view->canvasSelectionChanged();
            setSelectionAnchor(hit);
            if (hit->sessionId() != kInvalidSessionImageId) {
                emit m_view->sessionImageFocused(hit->sessionId());
            } else if (!hit->path().isEmpty()) {
                emit m_view->galleryItemFocused(hit->path());
            }
            event->accept();
            return true;
        }

        // Empty space: clear selection (keep Ctrl-additive empty no-ops).
        if (!ctrl) {
            m_view->canvasScene()->clearSelection();
            emit m_view->canvasSelectionChanged();
            emit m_view->statusChanged();
        }
        // Allow rubber-band start via base class when drag mode is RubberBandDrag.
        m_view->forwardGraphicsViewMousePress(event);
        return true;
    return true;
}


// --- Gallery key input (Tier 6e) ---

bool GalleryController::tryKeyPressGallery(QKeyEvent *event)
{
    // Gallery: arrow keys move among tiles by scene position; Enter opens.
    if (!m_view->isGalleryMode()
        || (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        || m_view->liveItems().isEmpty()) {
        return false;
    }

    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (ImageItem *item = m_view->selectedOrFirstGalleryItem()) {
            if (item->sessionId() != kInvalidSessionImageId) {
                emit m_view->sessionImageOpenRequested(item->sessionId());
            } else if (item->sessionIndex() >= 0) {
                emit m_view->sessionSlotOpenRequested(item->sessionIndex());
            } else if (!item->path().isEmpty()) {
                emit m_view->galleryItemOpenRequested(item->path());
            }
            event->accept();
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        ImageItem *item = (event->key() == Qt::Key_Home)
                              ? m_view->liveItems().first()
                              : m_view->liveItems().last();
        m_view->focusSessionPath(item->path());
        m_view->emitGalleryItemFocus(item);
        event->accept();
        return true;
    }

    // Spatial neighbour: prefer candidates in the arrow direction, score by
    // primary-axis distance with a cross-axis penalty (grid-friendly).
    const int key = event->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right
        && key != Qt::Key_Up && key != Qt::Key_Down) {
        return false;
    }
    ImageItem *from = m_view->selectedOrFirstGalleryItem();
    if (!from) {
        from = m_view->liveItems().first();
    }
    const QPointF origin = from->sceneBoundingRect().center();
    ImageItem *best = nullptr;
    qreal bestScore = 1e300;
    for (ImageItem *cand : m_view->liveItems()) {
        if (!cand || cand == from) {
            continue;
        }
        const QPointF c = cand->sceneBoundingRect().center();
        const auto scored = WorkspaceNavGeometry::scoreRelative(
            static_cast<Qt::Key>(key), origin, c);
        if (!scored.inDirection) {
            continue;
        }
        if (scored.score < bestScore) {
            bestScore = scored.score;
            best = cand;
        }
    }
    if (!best) {
        return false;
    }
    m_view->focusSessionPath(best->path());
    m_view->emitGalleryItemFocus(best);
    event->accept();
    return true;
}


// --- Gallery delete selection (Tier 6g) ---

bool GalleryController::tryKeyPressDeleteSelection(QKeyEvent *event)
{
    if (!m_view->isGalleryMode()) {
        return false;
    }
    if (event->key() != Qt::Key_Delete
        && !(event->key() == Qt::Key_Backspace && m_view->isMultiItemMode())) {
        return false;
    }
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return false;
    }
    const QList<QGraphicsItem *> selected = scene->selectedItems();
    QVector<SessionImageId> removeIds;
    QStringList removePaths;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (!m_view->liveItems().contains(item)) {
                continue;
            }
            if (item->sessionId() != kInvalidSessionImageId) {
                removeIds.append(item->sessionId());
            } else if (!item->path().isEmpty()) {
                removePaths.append(item->path());
            }
        }
    }
    if (removeIds.isEmpty() && removePaths.isEmpty()) {
        return false;
    }
    // Gallery tiles are the session — remove by id when bound.
    if (!removeIds.isEmpty()) {
        emit m_view->sessionRemoveIdsRequested(removeIds);
    }
    if (!removePaths.isEmpty()) {
        emit m_view->sessionRemovePathsRequested(removePaths);
    }
    event->accept();
    return true;
}


// --- Gallery soft decode window timers (Tier 5 residual) ---

void GalleryController::scheduleStatusRefresh(int delayMs)
{
    if (!m_view->isGalleryMode()) {
        emit m_view->statusChanged();
        return;
    }
    if (!m_statusRefreshTimer) {
        m_statusRefreshTimer = new QTimer(m_view);
        m_statusRefreshTimer->setSingleShot(true);
        QObject::connect(m_statusRefreshTimer, &QTimer::timeout, m_view, [this]() {
            if (m_view->isGalleryMode()) {
                updateSoftProgressHud();
                emit m_view->statusChanged();
            }
        });
    }
    m_statusRefreshTimer->setInterval(ViewTransform::nonNegMs(delayMs));
    m_statusRefreshTimer->start();
}

void GalleryController::scheduleDecodeWindowRefresh(int delayMs)
{
    if (!m_view->isGalleryMode()) {
        return;
    }
    if (!m_decodeScrollTimer) {
        m_decodeScrollTimer = new QTimer(m_view);
        m_decodeScrollTimer->setSingleShot(true);
        QObject::connect(m_decodeScrollTimer, &QTimer::timeout, m_view, [this]() {
            if (m_view->isGalleryMode()) {
                updateDecodeWindow();
            }
        });
    }
    // Restart with the requested delay (climb uses short; scroll may use longer).
    m_decodeScrollTimer->setInterval(ViewTransform::nonNegMs(delayMs));
    m_decodeScrollTimer->start();
}


// --- Gallery decode window body (Tier 5 residual) ---

int GalleryController::galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending)
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
    ordered.reserve(m_view->liveItems().size());
    for (ImageItem *item : m_view->liveItems()) {
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
        m_view->hostDisplayPipeline().installDisplayPixels(item, sample, kind, item->sessionId());
        int after = item->displayPixelLongEdge();
        if (after <= before && !hadDisplay && !sample.isNull()) {
            item->setPreviewImage(sample);
            after = item->displayPixelLongEdge();
        }
        if (after <= before && hadDisplay) {
            continue;
        }
        GallerySoftState &st = m_view->hostGallerySoftBook().state(path);
        // Shown edge only — hostEdge can exceed what install actually attached.
        st.have = GallerySoft::maxHave(st.have, after);
        item->update();
        ++installed;
    }
    return installed;
}

void GalleryController::updateDecodeWindow()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("updateGalleryDecodeWindow", 4);
    QElapsedTimer decodeWinTimer;
    if (m_view->hostPerf().isEnabled()) {
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
    if (!m_view->isGalleryMode() || m_view->liveItems().isEmpty()) {
        return;
    }
    // While sizes are still sequential, only allow blank LQIP installs from cache
    // — no tile ticks / pyramid (workers stay on ProbeSize).
    if (m_view->hostGallerySizeResolve().active()) {
        constexpr int kMaxInstallsDuringSizeResolve = GallerySoft::kMaxInstallsDuringSizeResolve;
        bool more = false;
        const int n = galleryInstallHostSoftOntoBlanks(kMaxInstallsDuringSizeResolve, &more);
        if (n > 0 && m_view->viewport()) {
            m_view->viewport()->update();
        }
        if (more) {
            scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowRearmMs);
        }
        return;
    }
    // Wall budget: cold open was stacking install + schedule + tile tick past
    // GUI_BUDGET. Slice work and re-arm instead of one multi-hundred-ms pass.
    QElapsedTimer wall;
    wall.start();
    constexpr qint64 kDecodeWindowWallMs = GallerySoft::kDecodeWindowWallMs;

    const QRect viewRect = m_view->viewport()->rect().adjusted(
        -GalleryPackFit::kDecodeOverscanPx, -GalleryPackFit::kDecodeOverscanPx,
        GalleryPackFit::kDecodeOverscanPx, GalleryPackFit::kDecodeOverscanPx);
    const QRectF sceneVisible = m_view->mapToScene(viewRect).boundingRect();

    qint64 usPass1 = 0;
    qint64 usPass2 = 0;
    qint64 usInterest = 0;
    QElapsedTimer phaseTimer;

    constexpr int kMaxInstallsPerDecodeWindow = GallerySoft::kMaxInstallsPerDecodeWindow;
    bool moreInstallsPending = false;
    if (m_view->hostPerf().isEnabled()) {
        phaseTimer.start();
    }
    const int hostInstalled =
        galleryInstallHostSoftOntoBlanks(kMaxInstallsPerDecodeWindow,
                                         &moreInstallsPending);
    if (hostInstalled > 0) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        scheduleStatusRefresh(GallerySoft::kStatusRefreshMs);
    }
    if (moreInstallsPending) {
        scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowRearmMs);
    }
    if (m_view->hostPerf().isEnabled()) {
        usPass1 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    // ------------------------------------------------------------------
    // Pass 2: LQIP install for blank cells (tiles owned by coordinator).
    // ------------------------------------------------------------------
    QStringList visible;
    QSet<QString> seen;

    // Prefer viewport hits only — full m_view->liveItems() + tileLodWanted was O(n) and
    // dominated cold decode windows on large sessions.
    const QList<QGraphicsItem *> hit =
        sceneVisible.isNull()
            ? QList<QGraphicsItem *>()
            : m_view->canvasScene()->items(sceneVisible, Qt::IntersectsItemBoundingRect);
    for (QGraphicsItem *gi : hit) {
        if (wall.elapsed() >= kDecodeWindowWallMs) {
            scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowSliceMs);
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

        GallerySoftState &st = m_view->hostGallerySoftBook().state(path);
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
            scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowSliceMs);
            break;
        }
        m_view->hostDisplayPipeline().scheduleGalleryDecode(path);
        ++scheduled;
    }
    if (m_view->hostPerf().isEnabled()) {
        usPass2 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    const bool lqipBusy = scheduled > 0 || moreInstallsPending;
    if (m_view->hostPerf().isEnabled()) {
        usInterest = phaseTimer.nsecsElapsed() / 1000;
    }

    // Tile issue: one coordinator tick per decode window (not per path).
    // Skip if the LQIP/schedule slice already burned the wall — re-arm instead.
    if (wall.elapsed() < kDecodeWindowWallMs) {
        int tileBudget = m_view->isGalleryMode() ? 32 : 8;
        m_view->hostDisplayPipeline().tickPrimaryTileLod(tileBudget);
    } else {
        scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowSliceMs);
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
        scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowSliceMs);
    }
    updateSoftProgressHud();
    if (m_view->hostPerf().isEnabled() && decodeWinTimer.isValid()) {
        m_view->hostPerf().noteDecodeWindowUs(decodeWinTimer.nsecsElapsed() / 1000);
        if (m_view->hostPerf().lastDecodeWindowSlow()) {
            fprintf(stderr,
                    "biltoo/perf: updateGalleryDecodeWindow %.1f ms "
                    "(max %.1f ms runs=%d items=%d "
                    "pass1=%.1f pass2=%.1f interest=%.1f install=%d)\n",
                    m_view->hostPerf().lastDecodeWindowUsValue() / 1000.0,
                    m_view->hostPerf().maxDecodeWindowUsValue() / 1000.0, m_view->hostPerf().decodeWindowRunsValue(),
                    static_cast<int>(m_view->liveItems().size()),
                    usPass1 / 1000.0, usPass2 / 1000.0, usInterest / 1000.0,
                    hostInstalled);
        }
    }
}


// --- Gallery pack (applyLayout) ---

void GalleryController::applyLayout(GalleryPackReason reason)
{
    ASSERT_GUI_THREAD();
    if (m_view->hostLayoutApply().active()) {
        return;
    }
    // Size-first open: do not pack on provisional stand-ins while probes run.
    if (m_view->hostGallerySizeResolve().active()) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    if (!m_view->isGalleryMode() || m_view->liveItems().isEmpty() || m_view->hostLayout().isFreeForm()) {
        return;
    }

    if (!m_view->pathOrderIsEmpty()) {
        const PackOrderView pack = m_view->currentPackOrder();
        m_view->reorderItemsByPaths(pack.paths());
    }

    // Gallery overview is axis-aligned. Strip any leftover Workspace placement
    // tilt/flips before packing (content 90°/flip remain in baked pixels).
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        ItemComponents::Placement pl = item->placement();
        pl.rotation = 0.0;
        pl.hFlip = false;
        pl.vFlip = false;
        item->applyPlacement(pl);
    }

    // Incremental packs (new session tiles, decode size change, F5) should keep
    // the user roughly in the same place. Enter / explicit layout switch still
    // starts at the origin. Image→Gallery restore uses pendingRestore instead.
    const bool preserveView =
        !pendingRestore()
        && (reason == GalleryPackReason::ContentChange
            || reason == GalleryPackReason::SessionMutate
            || reason == GalleryPackReason::Reload);
    // Scene coordinates are rewritten by pack — do NOT centerOn a pre-pack
    // scene point (that jumped the overview to the middle after crop). Keep
    // scrollbar pixel values instead.
    const int keptScrollH =
        (preserveView && m_view->horizontalScrollBar()) ? m_view->horizontalScrollBar()->value() : -1;
    const int keptScrollV =
        (preserveView && m_view->verticalScrollBar()) ? m_view->verticalScrollBar()->value() : -1;

    LayoutApplyGuard::Scoped layoutApplyScope(&m_view->hostLayoutApply());

    // Packaged layouts use view pixels as scene units so images scale to the window
    m_view->resetTransform();
    if (!pendingRestore() && !preserveView) {
        m_view->centerOn(0, 0);
    }

    // Reserve scrollbar space for the pack measurement. AsNeeded would let the
    // first bar appear, shrink the viewport, and leave the fitted axis slightly
    // oversized (dual bars). AlwaysOn only for this critical section; policy is
    // restored after sceneRect is set so Zoom Fit/Fill can hide unused bars.
    // m_view->hostLayoutApply() is already active — resizeEvent will not re-enter pack.
    const auto savedHBar = m_view->horizontalScrollBarPolicy();
    const auto savedVBar = m_view->verticalScrollBarPolicy();
    if (savedHBar != Qt::ScrollBarAlwaysOn || savedVBar != Qt::ScrollBarAlwaysOn) {
        m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    }

    const qreal margin = GalleryLayout::Params::kDefaultMargin;
    const qreal gap = GalleryLayout::Params::kDefaultGap;
    const qreal availW = GalleryPackFit::packAvailAxis(m_view->viewport()->width(), margin);
    const qreal availH = GalleryPackFit::packAvailAxis(m_view->viewport()->height(), margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_view->hostLayout().masonryColumnsValue();
    params.gridColumns = m_view->hostLayout().gridColumnsValue();
    params.masonryRows = m_view->hostLayout().masonryRowsValue();
    params.mode = GalleryPackFit::modeFromLayoutMode(m_view->hostLayout().currentMode());

    GalleryLayout::pack(m_view->liveItems(), params, [this](ImageItem *item) {
        m_view->itemWorld().setPathState(item->path(), m_view->captureState(item));
    });

    const QRectF bounds = ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(), margin);
    if (m_view->canvasScene()->sceneRect() != bounds) {
        m_view->canvasScene()->setSceneRect(bounds);
    }
    // Restore caller policy (AsNeeded/Off). With overshoot correction the packed
    // fitted axis should not need a bar; AsNeeded can hide it. Still under
    // m_view->hostLayoutApply() so a policy-driven resize does not repack.
    if (m_view->horizontalScrollBarPolicy() != savedHBar) {
        m_view->setHorizontalScrollBarPolicy(savedHBar);
    }
    if (m_view->verticalScrollBarPolicy() != savedVBar) {
        m_view->setVerticalScrollBarPolicy(savedVBar);
    }
    m_view->hostFraming().armFit();
    // Keep the guard until after statusChanged so slots cannot re-enter layout.
    emit m_view->statusChanged();
    // layoutApplyScope ends after this function returns (keeps guard through statusChanged)
    // Re-apply scroll after m_view->centerOn(0,0) above when returning from Image.
    applyPendingRestore();
    if (preserveView) {
        if (keptScrollH >= 0 && m_view->horizontalScrollBar()) {
            m_view->horizontalScrollBar()->setValue(keptScrollH);
        }
        if (keptScrollV >= 0 && m_view->verticalScrollBar()) {
            m_view->verticalScrollBar()->setValue(keptScrollV);
        }
    }
    // Explicit column changes: debounce setInterest (was multi-second stalls).
    // EnterGallery / Reload: run decode once now so startup is not blank until
    // the 180ms timer; still schedule a short follow-up for late soft.
    if (reason == GalleryPackReason::ExplicitLayout) {
        scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowAfterPackMs);
    } else if (reason == GalleryPackReason::EnterGallery
               || reason == GalleryPackReason::Reload) {
        updateDecodeWindow();
        scheduleDecodeWindowRefresh(GallerySoft::kDecodeWindowSettleMs);
    } else {
        updateDecodeWindow();
    }
}


// --- Gallery placeholders ---

void GalleryController::ensurePlaceholders()
{
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty()) {
        return;
    }
    // Only clear defer-populate. Keep size-resolve active so fill layouts still
    // wait for finishGallerySizeResolve to pack (soft may install meanwhile).
    m_view->hostGallerySoftBook().setDeferPopulate(false);
    QSet<ImageItem *> claimed;
    const PackOrderView pack = m_view->currentPackOrder();
    for (int i = 0; i < pack.size(); ++i) {
        const QString path = pack.pathAt(i);
        const SessionImageId sid = pack.idAt(i);

        ImageItem *existing = nullptr;
        if (sid != kInvalidSessionImageId) {
            existing = m_view->findItemBySessionId(sid);
        }
        if (!existing) {
            for (ImageItem *item : m_view->liveItems()) {
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
            const QSize sz = m_view->layoutSizeForPath(path, ImageCache::get(path));
            if (isPositiveSize(sz) && !m_view->hostSizeBook().isProvisional(path)) {
                existing->setIntrinsicSize(sz);
            }
            continue;
        }

        if (sid != kInvalidSessionImageId || i >= 0) {
            PendingSessionBind b;
            b.path = path;
            b.id = sid;
            b.index = i;
            m_view->hostBindBook().append(b);
            m_view->hostBindBook().setIndexForPath(path, i);
        }

        // Prefer definitive size; soft hint only if still provisional (should be rare).
        const QImage hint = ImageCache::get(path);
        const QSize sz = m_view->layoutSizeForPath(path, hint);
        ImageItem *ph = m_view->hostDisplayPipeline().createPlaceholderItem(path, sz);
        if (ph) {
            if (sid != kInvalidSessionImageId) {
                ph->setSessionId(sid);
            }
            ph->setSessionIndex(i);
            ph->setVisible(true);
            if (!hint.isNull()) {
                m_view->hostDisplayPipeline().installDisplayPixels(ph, hint,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     sid);
            }
            claimed.insert(ph);
        }
    }
    // Reuse the pack snapshot from the loop above (same generation; avoids -Wshadow).
    m_view->reorderItemsByPaths(pack.paths());
}


// --- Gallery soft watchdog + layout columns ---

void GalleryController::softWatchdogTick()
{
    if (!m_view->isGalleryMode() || m_view->liveItems().isEmpty()) {
        return;
    }
    // Soft PreferCache is gone. Watchdog only re-installs LQIP on blank
    // on-screen cells and keeps the tile coordinator awake.
    const QRectF sceneVisible =
        m_view->mapToScene(m_view->viewport()->rect().adjusted(-80, -80, 80, 80)).boundingRect();
    bool needWindow = false;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const QRectF tile = item->contentSceneRect();
        if (!tile.isNull() && tile.isValid() && !tile.intersects(sceneVisible)) {
            continue;
        }
        if (!item->hasDisplayPixels()) {
            m_view->hostDisplayPipeline().scheduleGalleryDecode(item->path());
            needWindow = true;
        }
    }
    if (needWindow) {
        updateDecodeWindow();
    } else {
        m_view->hostDisplayPipeline().tickPrimaryTileLod(48);
    }
    updateSoftProgressHud();
}

void GalleryController::updateSoftProgressHud()
{
    if (!m_view->isGalleryMode()) {
        return;
    }
    // LQIP is a free durable placeholder, not a user-facing "preview stage".
    // Never show "Improving previews… LQIP" — that was noise and mis-sold the product.
    if (m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Improving previews"))) {
        m_view->clearCentreProgress();
    }
}

void GalleryController::setGridColumns(int columns)
{
    const int before = m_view->hostLayout().gridColumnsValue();
    m_view->hostLayout().setGridColumns(columns);
    if (m_view->hostLayout().gridColumns == before) {
        return;
    }
    if (m_view->isGalleryMode()
        && (layoutIsGridFamily(m_view->hostLayout().currentMode())
            || layoutIsFlowFamily(m_view->hostLayout().currentMode())
            || m_view->hostLayout().currentMode() == LayoutMode::Facing)) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void GalleryController::setMasonryColumns(int columns)
{
    const int before = m_view->hostLayout().masonryColumnsValue();
    m_view->hostLayout().setMasonryColumns(columns);
    if (m_view->hostLayout().masonryColumns == before) {
        return;
    }
    if ((layoutIsMasonryColumns(m_view->hostLayout().currentMode()))
        && !m_view->liveItems().isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void GalleryController::setMasonryRows(int rows)
{
    const int before = m_view->hostLayout().masonryRowsValue();
    m_view->hostLayout().setMasonryRows(rows);
    if (m_view->hostLayout().masonryRows == before) {
        return;
    }
    if ((m_view->hostLayout().currentMode() == LayoutMode::MasonryRows || m_view->hostLayout().currentMode() == LayoutMode::MasonryRowsFill)
        && !m_view->liveItems().isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}


// --- Gallery mode canvas enter/leave helpers ---

void GalleryController::prepareCanvas()
{
    // Drop Image-mode fit transforms and prior layout scene rects so the previous
    // frame does not linger under the new packing (visible "ghost" between switches).
    m_view->hostUndoStack()->clear();
    m_view->canvasScene()->clearSelection();
    m_view->resetTransform();
    if (m_view->horizontalScrollBar()) {
        m_view->horizontalScrollBar()->setValue(0);
    }
    if (m_view->verticalScrollBar()) {
        m_view->verticalScrollBar()->setValue(0);
    }
    m_view->canvasScene()->setSceneRect(QRectF());
    m_view->hostFraming().setFitOnly();
    // Force a blank pass before items are re-packed.
    m_view->viewport()->update();
}

void GalleryController::invalidateDecodes()
{
    // Drop scheduled markers and pending path counts so late LoadAdd results
    // cannot create tiles after leaving Gallery. Bump generation so in-flight
    // pool jobs are rejected in onImageLoaded.
    m_view->hostDisplayPipeline().gallerySoftResetAll();
    m_view->hostDisplayPipeline().loadGate().clearPendingWorkspacePaths();
    m_view->hostDisplayPipeline().loadGate().bumpGeneration();
}


void GalleryController::setLayoutMode(LayoutMode mode)
{
    // Packaged layouts only; FreeForm is WorkspaceController::applyFreeFormLayout.
    if (mode == LayoutMode::FreeForm) {
        return;
    }

    // Packaged layout → Gallery only (enterGallery if needed).
    if (!m_view->isGalleryMode()) {
        m_view->enterGallery(mode);
        return;
    }

    if (m_view->hostLayout().isFreeForm() && mode != LayoutMode::FreeForm) {
        m_view->hostWorkspace().snapshotFreeFormStates();
    }

    m_view->hostLayout().setMode(mode);
    for (ImageItem *item : m_view->liveItems()) {
        m_view->applyItemModeFlags(item);
    }
    applyLayout(GalleryPackReason::EnterGallery);
}

void GalleryController::reloadFromDisk(bool relayout)
{
    QSet<QString> purgedPaths;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        m_view->hostDisplayPipeline().gallerySoftResetPath(path);
        if (!purgedPaths.contains(path)) {
            m_view->hostDisplayPipeline().purgeTilePathRam(path);
            purgedPaths.insert(path);
        } else {
            m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        }
        m_view->takePendingWorkspacePath(path);
        item->clearDecodedPixels();
        PendingSessionBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        m_view->hostBindBook().append(b);
        m_view->hostDisplayPipeline().scheduleGalleryDecode(path);
    }
    if (relayout) {
        applyLayout(GalleryPackReason::Reload);
    }
    m_view->flashHud(ImageView::tr("Reload"), ImageView::tr("Gallery"));
    emit m_view->statusChanged();
}

void GalleryController::hardReloadFromDisk(bool relayout)
{
    QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        targets = m_view->liveItems();
    }
    if (targets.isEmpty()) {
        return;
    }

    QSet<QString> pathSet;
    struct ReloadBind {
        QString path;
        SessionImageId id = kInvalidSessionImageId;
        int index = -1;
    };
    QList<ReloadBind> binds;
    int itemCount = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        ++itemCount;
        m_view->hostDisplayPipeline().gallerySoftResetPath(path);
        m_view->takePendingWorkspacePath(path);
        item->clearDecodedPixels();
        if (!pathSet.contains(path)) {
            ImageCache::remove(path);
            m_view->hostDisplayPipeline().purgeTilePathRam(path);
            for (int edge : ThumtooCache::kLadderEdges) {
                ThumtooCache::forgetPixelsSettled(path, edge);
            }
            pathSet.insert(path);
        } else {
            m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        }
        ReloadBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = item->sessionIndex();
        binds.append(b);
    }
    if (pathSet.isEmpty()) {
        return;
    }
    const QStringList paths = pathSet.values();
    const QString detail = (paths.size() == 1)
        ? QFileInfo(paths.constFirst()).fileName()
        : ImageView::tr("%1 paths · %2 items").arg(paths.size()).arg(itemCount);
    m_view->flashHud(ImageView::tr("Hard reload"), detail);

    auto remaining = std::make_shared<int>(paths.size());
    auto tileTotal = std::make_shared<qint64>(0);
    const bool doRelayout = relayout;

    auto finish = [this, binds, doRelayout, tileTotal]() {
        QSet<QString> probed;
        for (const ReloadBind &b : binds) {
            PendingSessionBind pending;
            pending.path = b.path;
            pending.id = b.id;
            pending.index = b.index;
            m_view->hostBindBook().append(pending);
            if (!probed.contains(b.path)) {
                ThumtooCache::scheduleProbe(b.path);
                probed.insert(b.path);
            }
            m_view->hostDisplayPipeline().scheduleGalleryDecode(b.path);
        }
        if (doRelayout) {
            applyLayout(GalleryPackReason::Reload);
        }
        if (*tileTotal > 0) {
            m_view->flashHud(ImageView::tr("Hard reload"),
                             ImageView::tr("%1 Store tiles removed").arg(*tileTotal));
        }
        emit m_view->statusChanged();
    };

    for (const QString &path : paths) {
        ThumtooCache::purgePathDurable(path, [remaining, tileTotal, finish](qint64 tiles) {
            *tileTotal += tiles;
            if (--(*remaining) == 0) {
                finish();
            }
        });
    }
}

void GalleryController::setRelayoutSuppressed(bool on)
{
    if (on) {
        m_view->hostGalleryRelayoutSuppress().push(true);
        if (m_view->hostLayoutDebounceTimer()) {
            m_view->hostLayoutDebounceTimer()->stop();
        }
    } else if (m_view->hostGalleryRelayoutSuppress().active()) {
        m_view->hostGalleryRelayoutSuppress().push(false);
    }
}
