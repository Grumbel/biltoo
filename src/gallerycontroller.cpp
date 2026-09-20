// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallerycontroller.h"
#include "imageview.h"
#include "imageview_types.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "viewtransform.h"
#include "workspacenavgeometry.h"
#include "gallerysoftsm.h"

#include <QScrollBar>
#include <QTimer>
#include <QSet>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QGraphicsScene>
#include <algorithm>

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
        m_view->setPathOrder(m_stashedPathOrder);
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
    // Image-mode navigation may have filled global path RAM; bind/paint without
    // waiting for the next decode-window timer.
    m_view->tickPrimaryTileLod(16);
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
    clearChrome();
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
            m_view->setPathOrder(livePaths);
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
    m_view->scheduleGalleryDecodeWindowRefresh(GallerySoft::kDecodeWindowScrollMs);
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

