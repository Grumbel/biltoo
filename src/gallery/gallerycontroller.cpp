// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallery/gallerycontroller.h"
#include <memory>
#include <QFileInfo>
#include "host/thumtoocache.h"
#include "imageview.h"
#include "session/packorderview.h"
#include "session/sessionbindbook.h"
#include "item/imagesizebook.h"
#include "imageview_types.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "item/itemframegeometry.h"
#include "item/itemcomponents.h"
#include "view/viewtransform.h"
#include "gallery/layoutapplyguard.h"
#include <QElapsedTimer>
#include "session/sessionappearance.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"
#include "gallery/gallerypackfit.h"
#include "display/imagecache.h"
#include "display/displayquality.h"
#include "workspace/workspacenavgeometry.h"
#include "gallery/gallerydecodesm.h"
#include <functional>

#include <QScrollBar>
#include <QTimer>
#include <QObject>
#include <QUndoStack>
#include <QSet>
#include <QHash>
#include <QMouseEvent>
#include <QDrag>
#include <QColor>
#include <QFontMetrics>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QMimeData>
#include <QUrl>
#include <QApplication>
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
        m_view->pathOrderSetOrder(m_stashedPackOrder.paths(), m_stashedPackOrder.ids());
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
        // bake onto the stash; if underlay still lags (or was never baked), force
        // rematerialize from the store so Gallery does not show full-frame.
        m_view->rematerializeGalleryItemFromStore(item);
    }
    {
        const PackOrderView pack = m_view->currentPackOrder();
        m_view->reorderItemsByPaths(pack.paths(), pack.ids());
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
        m_focusSessionId = sel->sessionId();
    }
}

void GalleryController::restoreViewport(const QString &focusPath, SessionImageId focusId)
{
    if (!focusPath.isEmpty()) {
        m_focusPath = focusPath;
    }
    if (focusId != kInvalidSessionImageId) {
        m_focusSessionId = focusId;
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
    if (m_focusSessionId != kInvalidSessionImageId) {
        focus = m_view->findItemBySessionId(m_focusSessionId);
    }
    if (!focus && !m_focusPath.isEmpty()) {
        focus = m_view->findItemForPath(m_focusPath);
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
    // Gallery → Image or Workspace: keep pack in Gallery stash (off-scene).
    // Do not destroy tiles — return restores the same cells (no rebuild flicker).
    // Workspace free-form tiles live in Workspace stash; these must never be
    // mixed (MODE_OWNERSHIP). Gallery::enter restores only Gallery stash.
    if (next == ImageView::ViewMode::Image
        || next == ImageView::ViewMode::Workspace) {
        // Image: keep scroll snapshot from snapshotViewport() when armed.
        // Workspace: drop scroll snapshot (free-form camera is separate).
        if (next == ImageView::ViewMode::Workspace) {
            m_haveScroll = false;
            m_haveViewCenter = false;
            m_pendingRestore = false;
        }
        stashItems();
    } else {
        m_haveScroll = false;
        m_haveViewCenter = false;
        m_view->hostGallery().invalidateDecodes();
        discardStash();
        m_view->clearLiveCanvas();
        m_view->pathOrderClear();
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

void GalleryController::returnFromImage(int layoutMode, const QString &focusPath,
                                        SessionImageId focusId)
{
    // Arm restore before enter/applyLayout so packs re-centre on the
    // snapshotted scene point (flags preserved across Gallery→Image leave).
    restoreViewport(focusPath, focusId);
    // Central leave → setActiveMode → enter (MODE_OWNERSHIP). enterGallery
    // routes through setViewMode when not already Gallery so previous=Image is
    // explicit and Gallery stash restore cannot race a stale mode flag.
    auto layout = static_cast<LayoutMode>(layoutMode);
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    m_view->enterGallery(layout);
    applyPendingRestore();
}

void GalleryController::enter(int packagedLayoutInt, int previousModeInt)
{
    auto packagedLayout = static_cast<LayoutMode>(packagedLayoutInt);
    if (packagedLayout == LayoutMode::FreeForm) {
        packagedLayout = LayoutMode::Masonry;
    }
    ImageView::ViewMode previous = ImageView::ViewMode::Gallery;
    if (previousModeInt >= 0) {
        previous = static_cast<ImageView::ViewMode>(previousModeInt);
    } else if (m_view->isWorkspaceMode()) {
        previous = ImageView::ViewMode::Workspace;
    } else if (m_view->isImageMode()) {
        previous = ImageView::ViewMode::Image;
    }

    // Preserve multi-select when only switching Gallery layout (not entering
    // from Image/Workspace — prepareGalleryCanvas clears selection).
    QStringList selectedPaths;
    QList<int> selectedIndices;
    QList<SessionImageId> selectedIds;
    QString anchorPath;
    int anchorIndex = -1;
    SessionImageId anchorId = kInvalidSessionImageId;
    const bool layoutSwitch = (previous == ImageView::ViewMode::Gallery)
        && m_view->isGalleryMode();

    // Restore Gallery stash when returning from Image or Workspace. Pack lives
    // only in Gallery m_stashedItems — never in Workspace stash — so this does
    // not put grid cells onto the free-form canvas.
    const bool restoredStash = !layoutSwitch
        && (previous == ImageView::ViewMode::Image
            || previous == ImageView::ViewMode::Workspace)
        && !m_stashedItems.isEmpty();
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
            if (ii->sessionId() != kInvalidSessionImageId) {
                selectedIds.append(ii->sessionId());
            } else {
                const int listIdx = m_view->sessionListIndex(ii);
                if (listIdx >= 0) {
                    selectedIndices.append(listIdx);
                } else if (!ii->path().isEmpty()) {
                    selectedPaths.append(ii->path());
                }
            }
        }
        if (m_selectionAnchor
            && m_view->liveItems().contains(m_selectionAnchor)
            && m_selectionAnchor->scene() == m_view->canvasScene()) {
            if (m_selectionAnchor->sessionId() != kInvalidSessionImageId) {
                anchorId = m_selectionAnchor->sessionId();
            } else {
                const int listIdx = m_view->sessionListIndex(m_selectionAnchor);
                if (listIdx >= 0) {
                    anchorIndex = listIdx;
                } else {
                    anchorPath = m_selectionAnchor->path();
                }
            }
        } else {
            m_selectionAnchor = nullptr;
        }
    }

    // Workspace leave is owned exclusively by setViewMode (leave → setActiveMode
    // → enter). Do not stash here — that path put Gallery packs into the
    // Workspace pointer stash and left Gallery empty after the next populate.
    // Free-form arrangement survives via Workspace m_stashedItems / m_savedItems.
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
        setPathOrderFromLiveItems();
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

    if (layoutSwitch
        && (!selectedIds.isEmpty() || !selectedIndices.isEmpty()
            || !selectedPaths.isEmpty())) {
        m_view->canvasScene()->clearSelection();
        for (const SessionImageId sid : selectedIds) {
            if (ImageItem *item = m_view->findItemBySessionId(sid)) {
                item->setSelected(true);
            }
        }
        for (const int listIdx : selectedIndices) {
            for (ImageItem *item : m_view->liveItems()) {
                if (item && m_view->sessionListIndex(item) == listIdx) {
                    item->setSelected(true);
                    break;
                }
            }
        }
        for (const QString &path : selectedPaths) {
            if (ImageItem *item = m_view->findItemForPath(path)) {
                item->setSelected(true);
            }
        }
        if (anchorId != kInvalidSessionImageId) {
            m_selectionAnchor = m_view->findItemBySessionId(anchorId);
        } else if (anchorIndex >= 0) {
            m_selectionAnchor = nullptr;
            for (ImageItem *item : m_view->liveItems()) {
                if (item && m_view->sessionListIndex(item) == anchorIndex) {
                    m_selectionAnchor = item;
                    break;
                }
            }
        } else if (!anchorPath.isEmpty()) {
            m_selectionAnchor = m_view->findItemForPath(anchorPath);
        } else {
            m_selectionAnchor = nullptr;
        }
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
    // Gallery: Ctrl+wheel zooms the view (inspection) and refreshes the
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
    // every high-res underlay, freezing the UI while zooming out.
    scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowScrollMs);
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
        m_view->emitGalleryItemFocus(hit);
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
            m_view->emitGalleryItemFocus(hit);
            m_dragArmed = true;
            m_dragStartViewPos = event->pos();
            m_dragPressItem = hit;
            event->accept();
            if (m_view->hostHudPrefs().isVisible() || m_view->hostHudFlash().isVisible()) {
                emit m_view->statusChanged();
            }
            return true;
        }

        if (hit && ctrl) {
            // Defensive: late tiles may have lost ItemIsSelectable.
            if (!(hit->flags() & QGraphicsItem::ItemIsSelectable)) {
                hit->setGallerySelectable(true);
            }
            hit->setSelected(!hit->isSelected());
            if (hit->isSelected()) {
                setSelectionAnchor(hit);
            }
            hit->invalidateDeviceCache();
            emit m_view->canvasSelectionChanged();
            m_view->emitGalleryItemFocus(hit);
            if (hit->isSelected()) {
                m_dragArmed = true;
                m_dragStartViewPos = event->pos();
                m_dragPressItem = hit;
            } else {
                clearGalleryDragArm();
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
            // Press on an already-selected tile keeps the multi-select so the
            // user can drag the whole set to reorder (filmstrip behaviour).
            if (hit->isSelected()) {
                setSelectionAnchor(hit);
                m_dragArmed = true;
                m_dragStartViewPos = event->pos();
                m_dragPressItem = hit;
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
            m_view->emitGalleryItemFocus(hit);
            // Arm reorder drag for the new single selection.
            m_dragArmed = true;
            m_dragStartViewPos = event->pos();
            m_dragPressItem = hit;
            event->accept();
            return true;
        }

        // Empty space: clear selection (keep Ctrl-additive empty no-ops).
        clearGalleryDragArm();
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



void GalleryController::clearGalleryDragArm()
{
    m_dragArmed = false;
    m_dragPressItem = nullptr;
    m_dragStartViewPos = QPoint();
}

bool GalleryController::tryMouseMoveGalleryDrag(QMouseEvent *event)
{
    if (!m_view->isGalleryMode() || !m_dragArmed) {
        return false;
    }
    if (!(event->buttons() & Qt::LeftButton)) {
        clearGalleryDragArm();
        return false;
    }
    const int dist = (event->pos() - m_dragStartViewPos).manhattanLength();
    if (dist < QApplication::startDragDistance()) {
        return false;
    }

    // Build ordered selection (session list order via liveItems order = pack order;
    // reorder uses SessionImageId → document index).
    QList<ImageItem *> selected;
    for (ImageItem *it : m_view->liveItems()) {
        if (it && it->isSelected()) {
            selected.append(it);
        }
    }
    if (selected.isEmpty() && m_dragPressItem) {
        selected.append(m_dragPressItem);
    }
    clearGalleryDragArm();
    if (selected.isEmpty()) {
        return false;
    }

    QStringList fullPaths;
    QByteArray idPayload;
    for (ImageItem *it : selected) {
        if (!it || it->path().isEmpty()) {
            continue;
        }
        fullPaths.append(it->path());
        const SessionImageId sid = it->sessionId();
        if (!idPayload.isEmpty()) {
            idPayload.append(',');
        }
        idPayload.append(QByteArray::number(static_cast<qint64>(sid)));
    }
    if (fullPaths.isEmpty()) {
        return false;
    }

    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-biltoo-paths"),
                  fullPaths.join(QLatin1Char('\n')).toUtf8());
    if (!idPayload.isEmpty()) {
        mime->setData(QStringLiteral("application/x-biltoo-session-ids"), idPayload);
    }
    // Placeholder URLs so generic acceptors see hasUrls().
    QList<QUrl> placeholders;
    for (int i = 0; i < fullPaths.size(); ++i) {
        placeholders.append(QUrl(QStringLiteral("about:biltoo-session/%1").arg(i)));
    }
    mime->setUrls(placeholders);

    QDrag drag(m_view);
    drag.setMimeData(mime);

    // Drag ghost: pick the sharpest available sample, then *always* normalize
    // to kEdge. Gallery LQIP/soft tiles are often ~16–32px; scaling only when
    // larger left those ghosts tiny.
    {
        ImageItem *previewSrc = selected.isEmpty() ? nullptr : selected.first();
        QImage best;
        auto consider = [&best](const QImage &img) {
            if (img.isNull()) {
                return;
            }
            const int le = qMax(img.width(), img.height());
            const int cur = best.isNull() ? 0 : qMax(best.width(), best.height());
            if (le > cur) {
                best = img;
            }
        };
        if (previewSrc) {
            if (!previewSrc->pixmap().isNull()) {
                consider(previewSrc->pixmap().toImage());
            }
            consider(previewSrc->displayImage());
            consider(previewSrc->sourceImage());
            if (!previewSrc->path().isEmpty()) {
                // Host soft cache may hold a larger sample than the live tile.
                consider(ImageCache::get(previewSrc->path(), /*minLongEdge=*/64));
                if (best.isNull()
                    || qMax(best.width(), best.height()) < 64) {
                    consider(ImageCache::get(previewSrc->path()));
                }
            }
        }
        QPixmap pix;
        if (!best.isNull()) {
            pix = QPixmap::fromImage(best);
        }
        if (!pix.isNull()) {
            constexpr int kEdge = 128;
            // Always target kEdge (upscale LQIP, downscale full) so the ghost
            // is a consistent, visible size under the cursor.
            if (qMax(pix.width(), pix.height()) != kEdge) {
                pix = pix.scaled(kEdge, kEdge, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
            }
            if (selected.size() > 1) {
                QPixmap badged(pix.width() + 8, pix.height() + 8);
                badged.fill(Qt::transparent);
                QPainter p(&badged);
                p.setRenderHint(QPainter::Antialiasing, true);
                p.setOpacity(0.92);
                p.drawPixmap(0, 0, pix);
                p.setOpacity(1.0);
                const QString label = QString::number(selected.size());
                QFont f = p.font();
                f.setBold(true);
                f.setPointSize(qMax(9, f.pointSize()));
                p.setFont(f);
                const QFontMetrics fm(f);
                const int pad = 4;
                const int bw = fm.horizontalAdvance(label) + pad * 2;
                const int bh = fm.height() + pad;
                const QRect badge(badged.width() - bw - 2, 2, bw, bh);
                p.setBrush(QColor(30, 30, 30, 220));
                p.setPen(Qt::NoPen);
                p.drawRoundedRect(badge, 6, 6);
                p.setPen(Qt::white);
                p.drawText(badge, Qt::AlignCenter, label);
                p.end();
                pix = badged;
            } else {
                QPixmap faded(pix.size());
                faded.fill(Qt::transparent);
                QPainter p(&faded);
                p.setOpacity(0.90);
                p.drawPixmap(0, 0, pix);
                p.end();
                pix = faded;
            }
            drag.setPixmap(pix);
            drag.setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
        }
    }

    // Prefer Move so drops are treated as reorder, not copy-append.
    drag.exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    event->accept();
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
            m_view->emitItemOpenInImageMode(item);
            event->accept();
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        ImageItem *item = (event->key() == Qt::Key_Home)
                              ? m_view->liveItems().first()
                              : m_view->liveItems().last();
        m_view->focusGalleryItem(item);
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
    m_view->focusGalleryItem(best);
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
    QList<int> removeIndices;
    QStringList removePaths;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (!m_view->liveItems().contains(item)) {
                continue;
            }
            if (item->sessionId() != kInvalidSessionImageId) {
                removeIds.append(item->sessionId());
            } else {
                const int listIdx = m_view->sessionListIndex(item);
                if (listIdx >= 0) {
                    removeIndices.append(listIdx);
                } else if (!item->path().isEmpty()) {
                    removePaths.append(item->path());
                }
            }
        }
    }
    if (removeIds.isEmpty() && removeIndices.isEmpty() && removePaths.isEmpty()) {
        return false;
    }
    // Gallery tiles are the session — prefer id, then list index, path last.
    if (!removeIds.isEmpty()) {
        emit m_view->sessionRemoveIdsRequested(removeIds);
    }
    if (!removeIndices.isEmpty()) {
        emit m_view->sessionRemoveIndicesRequested(removeIndices);
    }
    if (!removePaths.isEmpty()) {
        emit m_view->sessionRemovePathsRequested(removePaths);
    }
    event->accept();
    return true;
}


// --- Gallery decode window timers ---

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
                syncVirtualWindow();
                updateDecodeWindow();
            }
        });
    }
    // Restart with the requested delay (climb uses short; scroll may use longer).
    m_decodeScrollTimer->setInterval(ViewTransform::nonNegMs(delayMs));
    m_decodeScrollTimer->start();
}


// --- Gallery decode window body (Tier 5 residual) ---

int GalleryController::galleryInstallLqipOntoBlanks(int maxInstalls, bool *morePending)
{
    if (morePending) {
        *morePending = false;
    }
    if (maxInstalls <= 0) {
        return 0;
    }
    int installed = 0;
    // Underlay only (LQIP / EMB) — never install PreferCache whole-frame into cells.
    // Prefer viewport hits: full liveItems walks dominated idle decode windows on
    // large sessions (O(n) every 16 ms re-arm with nothing to install).
    QList<ImageItem *> ordered;
    ordered.reserve(64);
    QRectF sceneVisible;
    if (m_view->viewport()) {
        sceneVisible = m_view->mapToScene(
            m_view->viewport()->rect().adjusted(
                -GalleryPackFit::kDecodeOverscanPx, -GalleryPackFit::kDecodeOverscanPx,
                GalleryPackFit::kDecodeOverscanPx, GalleryPackFit::kDecodeOverscanPx))
            .boundingRect();
    }
    const QList<QGraphicsItem *> hit =
        sceneVisible.isNull()
            ? QList<QGraphicsItem *>()
            : m_view->canvasScene()->items(sceneVisible, Qt::IntersectsItemBoundingRect);
    QSet<ImageItem *> seen;
    for (QGraphicsItem *gi : hit) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->path().isEmpty() || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        const int e = item->displayPixelLongEdge();
        // Allow EMB band (≤320) so EXIF/PDF /Thumb can replace a tiny ThumbHash.
        if (!item->hasDisplayPixels()
            || e <= DisplayQuality::kEmbeddedUnderlayMaxEdge) {
            ordered.append(item);
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
        // Underlay only — downscale huge host samples for install paint, but do
        // not put a smaller raster back into ImageCache (that wiped EMB EXIF).
        if (sampleEdge > DisplayQuality::kEmbeddedUnderlayMaxEdge) {
            if (item->hasDisplayPixels()
                && shown >= DisplayQuality::kEmbeddedUnderlayMaxEdge) {
                continue;
            }
            const int cap = DisplayQuality::kEmbeddedUnderlayMaxEdge;
            sample = hostSample.scaled(
                cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            sampleEdge = ImageCache::longEdge(sample);
            if (sample.isNull() || sampleEdge <= 0) {
                continue;
            }
            // Install-only scale — leave ImageCache at the larger EMB/HOST sample.
        }
        const SessionAppearance::PixelKind kind =
            SessionAppearance::PixelKind::SoftPreview;
        const int before = shown;
        const bool hadDisplay = item->hasDisplayPixels();
        m_view->hostDisplayPipeline().installDisplayPixels(item, sample, kind, item->sessionId());
        int after = item->displayPixelLongEdge();
        if (after <= before && !hadDisplay && !sample.isNull()) {
            m_view->setItemPreviewImage(item, sample);
            after = item->displayPixelLongEdge();
        }
        if (after <= before && hadDisplay) {
            continue;
        }
        GalleryDecodeState &st = m_view->hostGalleryDecodeBook().state(path);
        // Shown edge only — hostEdge can exceed what install actually attached.
        st.have = GalleryDecode::maxHave(st.have, after);
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
        constexpr int kMaxInstallsDuringSizeResolve = GalleryDecode::kMaxInstallsDuringSizeResolve;
        bool more = false;
        const int n = galleryInstallLqipOntoBlanks(kMaxInstallsDuringSizeResolve, &more);
        if (n > 0 && m_view->viewport()) {
            m_view->viewport()->update();
        }
        if (more) {
            scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowRearmMs);
        }
        return;
    }
    // Wall budget: cold open was stacking install + schedule + tile tick past
    // GUI_BUDGET. Slice work and re-arm instead of one multi-hundred-ms pass.
    QElapsedTimer wall;
    wall.start();
    constexpr qint64 kDecodeWindowWallMs = GalleryDecode::kDecodeWindowWallMs;

    const QRect viewRect = m_view->viewport()->rect().adjusted(
        -GalleryPackFit::kDecodeOverscanPx, -GalleryPackFit::kDecodeOverscanPx,
        GalleryPackFit::kDecodeOverscanPx, GalleryPackFit::kDecodeOverscanPx);
    const QRectF sceneVisible = m_view->mapToScene(viewRect).boundingRect();

    qint64 usPass1 = 0;
    qint64 usPass2 = 0;
    qint64 usInterest = 0;
    QElapsedTimer phaseTimer;

    constexpr int kMaxInstallsPerDecodeWindow = GalleryDecode::kMaxInstallsPerDecodeWindow;
    bool moreInstallsPending = false;
    if (m_view->hostPerf().isEnabled()) {
        phaseTimer.start();
    }
    const int hostInstalled =
        galleryInstallLqipOntoBlanks(kMaxInstallsPerDecodeWindow,
                                         &moreInstallsPending);
    if (hostInstalled > 0) {
        if (m_view->viewport()) {
            m_view->viewport()->update();
        }
        scheduleStatusRefresh(GalleryDecode::kStatusRefreshMs);
    }
    if (moreInstallsPending) {
        scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowRearmMs);
    }
    if (m_view->hostPerf().isEnabled()) {
        usPass1 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    // ------------------------------------------------------------------
    // Pass 2: LQIP install for blank cells (tiles owned by coordinator).
    // ------------------------------------------------------------------
    bool needSlice = false;
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
            needSlice = true;
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

        GalleryDecodeState &st = m_view->hostGalleryDecodeBook().state(path);
        st.have = GalleryDecode::maxHave(st.have, item->displayPixelLongEdge());
        st.terminal = true;

        // Blank on-screen cells only — off-screen waits until scrolled in.
        if (!item->hasDisplayPixels()) {
            visible.append(path);
        }
    }

    constexpr int kSchedBudget = 32;
    int scheduled = 0;
    int realWork = 0;
    for (const QString &path : visible) {
        if (scheduled >= kSchedBudget || wall.elapsed() >= kDecodeWindowWallMs) {
            needSlice = true;
            break;
        }
        // Count only real probe/LQIP/pyramid starts — blank cells that are
        // already waiting on workers must not re-arm every 16 ms forever.
        if (m_view->hostDisplayPipeline().scheduleGalleryDecode(path)) {
            ++realWork;
        }
        ++scheduled;
    }
    if (m_view->hostPerf().isEnabled()) {
        usPass2 = phaseTimer.nsecsElapsed() / 1000;
        phaseTimer.restart();
    }

    const bool lqipBusy = realWork > 0 || moreInstallsPending;
    if (m_view->hostPerf().isEnabled()) {
        usInterest = phaseTimer.nsecsElapsed() / 1000;
    }

    // Tile issue: one coordinator tick per decode window (not per path).
    // Skip if the LQIP/schedule slice already burned the wall — mark needSlice.
    if (wall.elapsed() < kDecodeWindowWallMs) {
        int tileBudget = m_view->isGalleryMode() ? 32 : 8;
        m_view->hostDisplayPipeline().tickPrimaryTileLod(tileBudget);
    } else {
        needSlice = true;
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

    // Re-arm only when this pass started real work or hit the wall mid-slice.
    // Blank on-screen cells waiting on workers must not spin every 16 ms
    // (that was idle GUI_BUDGET spam on large galleries). Tile coverage
    // continues via TileLoadCoordinator re-arm / completion / watchdog.
    if (needSlice || realWork > 0 || moreInstallsPending) {
        scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSliceMs);
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
    GUI_BUDGET_MS("GalleryController::applyLayout", 12);
    if (m_view->hostLayoutApply().active()) {
        return;
    }
    // Size-first open: while the gate is active, liveItems are only the ordered
    // sized prefix (ensurePlaceholders stops at the first unresolved path). Pack
    // that prefix so cells are not stacked at the origin until the gate completes.
    // Provisional stand-ins are no longer installed in Gallery, so packing here
    // is safe. Full session pack still runs on gate complete (EnterGallery).
    if (m_view->hostGallerySizeResolve().active()
        && reason != GalleryPackReason::ContentChange
        && reason != GalleryPackReason::EnterGallery
        && reason != GalleryPackReason::Reload) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    // Virtualized: path order may be full while liveItems is still empty.
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty() || m_view->hostLayout().isFreeForm()) {
        return;
    }

    if (!m_view->pathOrderIsEmpty()) {
        const PackOrderView pack = m_view->currentPackOrder();
        m_view->reorderItemsByPaths(pack.paths(), pack.ids());
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
        !m_pendingRestore
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
    if (!m_pendingRestore && !preserveView) {
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

    // Progressive packs during the size gate only need scene poses for display.
    // Writing ItemWorld for every cell every ~50–120ms was pure overhead (and
    // EnterGallery / gate-complete pack persists the final poses once).
// Virtualized pack: poses for the full session, live items only in the window.
    rebuildVirtualPlan();
    syncVirtualWindow();
    for (ImageItem *item : m_view->liveItems()) {
        if (item) {
            item->setVisible(true);
        }
    }

    const QRectF bounds = m_virtualSceneBounds.isValid()
        ? m_virtualSceneBounds
        : ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(), margin);
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
    // Progressive packs during the size gate fire every ~16–40ms. Emitting
    // statusChanged each time runs MainWindow::updateStatus (TOC/metadata/
    // adjustments + O(n) pendingDecodeCount) and freezes the GUI for the whole
    // probe stream. Gate-complete / EnterGallery / explicit packs still notify.
    const bool progressiveDuringGate =
        m_view->hostGallerySizeResolve().active()
        && reason == GalleryPackReason::ContentChange;
    if (!progressiveDuringGate) {
        // Keep the guard until after statusChanged so slots cannot re-enter layout.
        emit m_view->statusChanged();
    }
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
    // the 180ms timer; still schedule a short follow-up for late LQIP.
    if (reason == GalleryPackReason::ExplicitLayout) {
        scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowAfterPackMs);
    } else if (reason == GalleryPackReason::EnterGallery
               || reason == GalleryPackReason::Reload) {
        updateDecodeWindow();
        scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowSettleMs);
    } else {
        updateDecodeWindow();
    }
}


// --- Gallery placeholders ---

bool GalleryController::ensurePlaceholders()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("GalleryController::ensurePlaceholders", 8);
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty()) {
        return false;
    }
    // Size gate: only plan rows that already have definitive sizes (prefix).
    rebuildVirtualPlan();
    syncVirtualWindow();
    return false;
}

void GalleryController::rebuildVirtualPlan()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("GalleryController::rebuildVirtualPlan", 16);
    m_virtualSlots.clear();
    m_virtualSceneBounds = QRectF();
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty()
        || m_view->hostLayout().isFreeForm()) {
        return;
    }

    const PackOrderView pack = m_view->currentPackOrder();
    const bool sizeGate = m_view->hostGallerySizeResolve().active();
    const ImageSizeBook &book = m_view->hostSizeBook();

    QVector<QSizeF> sizes;
    sizes.reserve(pack.size());
    m_virtualSlots.reserve(pack.size());

    for (int i = 0; i < pack.size(); ++i) {
        const QString path = pack.pathAt(i);
        const SessionImageId sid = pack.idAt(i);
        if (sizeGate) {
            if (layoutNeedsAllSizes(m_view->hostLayout().currentMode())) {
                // Wait for full gate — empty plan until complete.
                if (!book.hasDefinitive(path) && !book.isFailed(path)) {
                    m_virtualSlots.clear();
                    return;
                }
            } else if (!book.hasDefinitive(path) && !book.isFailed(path)) {
                // Ordered progressive: stop plan at first unresolved (prefix only).
                break;
            }
        }
        // Orient-aware layout size without Store I/O (allowStoreAppearance=false).
        // book.known alone dropped 90°/flip aspect; full contentLayoutSize could
        // hit loadContentAppearance per path and froze large opens for minutes.
        QSize lay = m_view->contentLayoutSize(path, sid, /*allowStoreAppearance=*/false);
        if (!isPositiveSize(lay) || lay.width() <= 1) {
            lay = book.known(path);
        }
        if (!isPositiveSize(lay) || lay.width() <= 1) {
            lay = ImageSizeBook::standInNeutral();
        }
        VirtualSlot slot;
        slot.path = path;
        slot.id = sid;
        slot.layoutSize = GalleryLayout::layoutSizeForNative(QSizeF(lay), 0.0);
        m_virtualSlots.append(slot);
        sizes.append(slot.layoutSize);
    }

    if (m_virtualSlots.isEmpty()) {
        return;
    }

    const qreal margin = GalleryLayout::Params::kDefaultMargin;
    const qreal gap = GalleryLayout::Params::kDefaultGap;
    const qreal availW = GalleryPackFit::packAvailAxis(
        m_view->viewport() ? m_view->viewport()->width() : 800, margin);
    const qreal availH = GalleryPackFit::packAvailAxis(
        m_view->viewport() ? m_view->viewport()->height() : 600, margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_view->hostLayout().masonryColumnsValue();
    params.gridColumns = m_view->hostLayout().gridColumnsValue();
    params.masonryRows = m_view->hostLayout().masonryRowsValue();
    params.mode = GalleryPackFit::modeFromLayoutMode(m_view->hostLayout().currentMode());

    const QVector<GalleryLayout::PackPose> poses =
        GalleryLayout::packPosesForMode(params.mode, sizes, params);
    QRectF bounds;
    for (int i = 0; i < m_virtualSlots.size() && i < poses.size(); ++i) {
        VirtualSlot &slot = m_virtualSlots[i];
        const GalleryLayout::PackPose &pose = poses.at(i);
        slot.center = pose.center;
        slot.scale = pose.scale;
        slot.cellSize = pose.cellSize;
        QSizeF cell = slot.cellSize;
        if (cell.isEmpty()) {
            cell = QSizeF(slot.layoutSize.width() * slot.scale,
                          slot.layoutSize.height() * slot.scale);
        }
        slot.bounds = QRectF(slot.center.x() - cell.width() / 2.0,
                             slot.center.y() - cell.height() / 2.0,
                             cell.width(), cell.height());
        bounds = bounds.united(slot.bounds);
    }
    m_virtualSceneBounds = ViewTransform::padded(bounds, margin);
    ++m_virtualPlanGeneration;
}

void GalleryController::syncVirtualWindow()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET_MS("GalleryController::syncVirtualWindow", 12);
    if (!m_view->isGalleryMode() || m_virtualSlots.isEmpty()) {
        return;
    }

    QRectF sceneVis;
    if (m_view->viewport()) {
        const QRect vr = m_view->viewport()->rect().adjusted(
            -GalleryPackFit::kDecodeOverscanPx * 3,
            -GalleryPackFit::kDecodeOverscanPx * 3,
            GalleryPackFit::kDecodeOverscanPx * 3,
            GalleryPackFit::kDecodeOverscanPx * 3);
        sceneVis = m_view->mapToScene(vr).boundingRect();
    }
    if (sceneVis.isNull() || !sceneVis.isValid()) {
        // Before first show: materialize a small prefix so the window is not empty.
        sceneVis = m_virtualSceneBounds;
        if (sceneVis.height() > 2400.0) {
            sceneVis.setHeight(2400.0);
        }
        if (sceneVis.width() > 4000.0) {
            sceneVis.setWidth(4000.0);
        }
    }

    QSet<int> want;
    for (int i = 0; i < m_virtualSlots.size(); ++i) {
        if (m_virtualSlots.at(i).bounds.intersects(sceneVis)) {
            want.insert(i);
        }
    }
    // Hard cap — pathological dense packs.
    constexpr int kMaxLive = 180;
    if (want.size() > kMaxLive) {
        QList<int> ranked = want.values();
        const QPointF c = sceneVis.center();
        std::sort(ranked.begin(), ranked.end(), [&](int a, int b) {
            const QPointF ca = m_virtualSlots.at(a).bounds.center();
            const QPointF cb = m_virtualSlots.at(b).bounds.center();
            const qreal da = QPointF(ca - c).manhattanLength();
            const qreal db = QPointF(cb - c).manhattanLength();
            return da < db;
        });
        want.clear();
        for (int i = 0; i < kMaxLive && i < ranked.size(); ++i) {
            want.insert(ranked.at(i));
        }
    }

    // Index live items by session id / path occurrence.
    QHash<SessionImageId, ImageItem *> byId;
    QMultiHash<QString, ImageItem *> byPath;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        if (item->sessionId() != kInvalidSessionImageId) {
            byId.insert(item->sessionId(), item);
        } else {
            byPath.insert(item->path(), item);
        }
    }

    QSet<ImageItem *> keep;
    for (int idx : want) {
        const VirtualSlot &slot = m_virtualSlots.at(idx);
        ImageItem *item = nullptr;
        if (slot.id != kInvalidSessionImageId) {
            item = byId.value(slot.id, nullptr);
        }
        if (!item && !slot.path.isEmpty()) {
            auto it = byPath.find(slot.path);
            if (it != byPath.end()) {
                item = it.value();
                byPath.erase(it);
            }
        }
        if (!item) {
            // Lazy size probe for on-screen rows not covered by the open prefix.
            if (!slot.path.isEmpty()
                && !m_view->hostSizeBook().hasDefinitive(slot.path)
                && !m_view->hostSizeBook().isFailed(slot.path)) {
                ThumtooCache::scheduleProbe(slot.path);
            }
            const QSize sz = slot.layoutSize.toSize();
            item = m_view->hostDisplayPipeline().createPlaceholderItem(
                slot.path, isPositiveSize(sz) ? sz : ImageSizeBook::standInNeutral());
            if (!item) {
                continue;
            }
            if (slot.id != kInvalidSessionImageId) {
                m_view->setItemSessionId(item, slot.id);
            }
            if (m_view->sessionListIndex(item) < 0) {
                item->setSessionIndex(idx);
            }
            item->setVisible(true);
            if (slot.id != kInvalidSessionImageId) {
                byId.insert(slot.id, item);
            }
        }
        keep.insert(item);
        // Apply packed pose.
        if (!slot.cellSize.isEmpty()) {
            GalleryLayout::setItemGalleryCellSize(item, slot.cellSize);
        } else {
            GalleryLayout::setItemGalleryCellSize(item, {});
        }
        ItemComponents::Placement pl = item->placement();
        pl.pos = slot.center;
        pl.scale = slot.scale;
        pl.scaleY = slot.scale;
        pl.shear = 0.0;
        pl.rotation = 0.0;
        pl.hFlip = false;
        pl.vFlip = false;
        pl.opacity = 1.0;
        GalleryLayout::applyItemPlacement(item, pl);
    }

    // Drop live items outside the window.
    QList<ImageItem *> doomed;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && !keep.contains(item)) {
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        m_view->hostDisplayPipeline().galleryDecodeResetPath(item->path());
        m_view->destroyCanvasItem(item);
    }

    if (m_view->canvasScene() && m_virtualSceneBounds.isValid()) {
        if (m_view->canvasScene()->sceneRect() != m_virtualSceneBounds) {
            m_view->canvasScene()->setSceneRect(m_virtualSceneBounds);
        }
    }
}

void GalleryController::paintVirtualPlaceholders(QPainter *painter, const QRectF &exposed) const
{
    if (!painter || !m_view || !m_view->isGalleryMode() || m_virtualSlots.isEmpty()) {
        return;
    }
    // Same no-LQIP chrome as live ImageItem blanks (ItemFrameGeometry).
    // Live items paint on top when present; empty cells match that look.
    painter->save();
    int drawn = 0;
    constexpr int kMaxDrawn = 400; // exposed region only; hard cap for safety
    for (const VirtualSlot &slot : m_virtualSlots) {
        if (!slot.bounds.intersects(exposed)) {
            continue;
        }
        ItemFrameGeometry::paintNeutralPlaceholder(painter, slot.bounds);
        if (++drawn >= kMaxDrawn) {
            break;
        }
    }
    painter->restore();
}

void GalleryController::decodeWatchdogTick()
{
    if (!m_view->isGalleryMode() || m_view->liveItems().isEmpty()) {
        return;
    }
    // Size gate owns workers — LQIP only, never tile ticks/pyramids.
    if (m_view->hostGallerySizeResolve().active()) {
        updateDecodeWindow();
        return;
    }
    // Soft PreferCache is gone. Watchdog only re-installs LQIP on blank
    // on-screen cells and keeps the tile coordinator awake.
    // Viewport hit-test only — full liveItems walks were O(n) every second.
    const QRectF sceneVisible =
        m_view->mapToScene(m_view->viewport()->rect().adjusted(-80, -80, 80, 80)).boundingRect();
    bool needWindow = false;
    const QList<QGraphicsItem *> hit =
        sceneVisible.isNull()
            ? QList<QGraphicsItem *>()
            : m_view->canvasScene()->items(sceneVisible, Qt::IntersectsItemBoundingRect);
    QSet<ImageItem *> seen;
    for (QGraphicsItem *gi : hit) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->path().isEmpty() || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        if (!item->hasDisplayPixels()) {
            if (m_view->hostDisplayPipeline().scheduleGalleryDecode(item->path())) {
                needWindow = true;
            }
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
    // Size gate owns the centre HUD while probes run.
    if (m_view->hostGallerySizeResolve().active()) {
        return;
    }
    // On-screen set must match the decode window (scene hit-test), not a manual
    // contentSceneRect walk. Null/invalid rects were counted as on-screen and
    // "have pixels" was only soft underlay — tiles-only cells looked filled but
    // stayed blank forever (e.g. frozen 41/73).
    const QRectF sceneVisible =
        m_view->mapToScene(
            m_view->viewport()->rect().adjusted(
                -GalleryPackFit::kDecodeOverscanPx, -GalleryPackFit::kDecodeOverscanPx,
                GalleryPackFit::kDecodeOverscanPx, GalleryPackFit::kDecodeOverscanPx))
            .boundingRect();
    int blank = 0;
    int total = 0;
    const QList<QGraphicsItem *> hit =
        sceneVisible.isNull()
            ? QList<QGraphicsItem *>()
            : m_view->canvasScene()->items(sceneVisible, Qt::IntersectsItemBoundingRect);
    QSet<ImageItem *> seen;
    for (QGraphicsItem *gi : hit) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->path().isEmpty() || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        ++total;
        // Ready = soft underlay or live/covered tile paint (user sees content).
        const bool ready = item->hasDisplayPixels()
            || item->tileLodActive()
            || item->tileLodViewportCovered()
            || item->tileLodHasPathRam();
        if (!ready) {
            ++blank;
        }
    }
    if (total == 0 || blank == 0) {
        if (m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Loading tiles"))
            || m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Improving previews"))) {
            m_view->clearCentreProgress();
        }
        return;
    }
    // Only show when a meaningful fraction is still blank (avoid flicker on one
    // cell). Clear a prior chip so we do not leave "Loading tiles…" stuck at
    // 1 residual blank forever.
    if (blank < 2 && total > 8) {
        if (m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Loading tiles"))
            || m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Improving previews"))) {
            m_view->clearCentreProgress();
        }
        return;
    }
    const QString detail =
        m_view->tr("%1 / %2 on-screen cells ready").arg(total - blank).arg(total);
    m_view->setCentreProgress(m_view->tr("Loading tiles…"), detail);
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
    m_view->hostDisplayPipeline().galleryDecodeResetAll();
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
        m_view->hostDisplayPipeline().galleryDecodeResetPath(path);
        if (!purgedPaths.contains(path)) {
            m_view->hostDisplayPipeline().purgeTilePathRam(path);
            purgedPaths.insert(path);
        } else {
            m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        }
        m_view->takePendingWorkspacePath(path);
        m_view->clearItemDecodedPixels(item);
        PendingSessionBind b;
        b.path = path;
        b.id = item->sessionId();
        b.index = m_view->sessionListIndex(item);
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
        m_view->hostDisplayPipeline().galleryDecodeResetPath(path);
        m_view->takePendingWorkspacePath(path);
        m_view->clearItemDecodedPixels(item);
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
        b.index = m_view->sessionListIndex(item);
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

void GalleryController::setPathOrderFromLiveItems()
{
    QStringList paths;
    QVector<SessionImageId> ids;
    paths.reserve(m_view->liveItems().size());
    ids.reserve(m_view->liveItems().size());
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        paths.append(item->path());
        ids.append(item->sessionId());
    }
    if (!paths.isEmpty()) {
        m_view->pathOrderSetOrder(paths, ids);
    }
}
