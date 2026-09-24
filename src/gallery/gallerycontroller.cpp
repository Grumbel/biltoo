// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gallery/gallerycontroller.h"
#include <QPen>
#include "gallery/gallerylayout.h"
#include <QWidget>
#include "gallery/gallerydecodesm.h"
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
#include "content/contentxform.h"
#include "util/biltoo_logging.h"
#include "util/biltoo_thread.h"
#include "gallery/gallerypackfit.h"
#include "display/imagecache.h"
#include "display/displayquality.h"
#include "workspace/workspacenavgeometry.h"
#include "gallery/gallerydecodesm.h"
#include <functional>

#include <QScrollBar>
#include <QAbstractScrollArea>
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


namespace {

/**
 * Pack measure viewport.
 * - AlwaysOff / AlwaysOn: measure the live viewport (no policy change).
 * - AsNeeded: force AlwaysOn on both axes so the pack reserves both gutters.
 *   Packing to the full AsNeeded client then showing one bar shrinks the other
 *   axis and can force a dual-bar loop.
 * AlwaysOff must NOT force AlwaysOn: that measured a gutter-shrunken size, then
 * restored a full client — AlignCenter floated the pack (off-centre “scrollbar”
 * margins after Image→Gallery).
 */
class PackViewportGuard
{
public:
    explicit PackViewportGuard(QAbstractScrollArea *view)
        : m_view(view)
    {
        if (!m_view) {
            return;
        }
        m_savedH = m_view->horizontalScrollBarPolicy();
        m_savedV = m_view->verticalScrollBarPolicy();
        const bool asNeeded = (m_savedH == Qt::ScrollBarAsNeeded
                               || m_savedV == Qt::ScrollBarAsNeeded);
        if (asNeeded
            && (m_savedH != Qt::ScrollBarAlwaysOn
                || m_savedV != Qt::ScrollBarAlwaysOn)) {
            m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
            m_forced = true;
        }
        if (QWidget *vp = m_view->viewport()) {
            m_width = vp->width();
            m_height = vp->height();
        }
    }

    PackViewportGuard(const PackViewportGuard &) = delete;
    PackViewportGuard &operator=(const PackViewportGuard &) = delete;

    ~PackViewportGuard() { restore(); }

    void restore()
    {
        if (!m_view) {
            return;
        }
        if (m_forced) {
            if (m_view->horizontalScrollBarPolicy() != m_savedH) {
                m_view->setHorizontalScrollBarPolicy(m_savedH);
            }
            if (m_view->verticalScrollBarPolicy() != m_savedV) {
                m_view->setVerticalScrollBarPolicy(m_savedV);
            }
        }
        m_view = nullptr;
        m_forced = false;
    }

    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }

private:
    QAbstractScrollArea *m_view = nullptr;
    Qt::ScrollBarPolicy m_savedH = Qt::ScrollBarAsNeeded;
    Qt::ScrollBarPolicy m_savedV = Qt::ScrollBarAsNeeded;
    int m_width = 0;
    int m_height = 0;
    bool m_forced = false;
};

} // namespace

GalleryController::GalleryController(ImageView *view)
    : m_view(view)
    , m_sizeResolve(this, view)
{
    // Parent timer to the shell so lifetime tracks ImageView.
    m_layoutDebounceTimer = new QTimer(m_view);
    m_layoutDebounceTimer->setSingleShot(true);
    m_layoutDebounceTimer->setInterval(LayoutDebounce::kIntervalMs);
    QObject::connect(m_layoutDebounceTimer, &QTimer::timeout, m_view, [this]() {
        GalleryPackReason reason = GalleryPackReason::ContentChange;
        if (m_view->isGalleryMode() && !m_layout.isFreeForm()
            && m_layoutDebounce.take(&reason)) {
            if (m_sizeResolve.active()) {
                ensurePlaceholders();
            }
            applyLayout(reason);
        }
    });
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
    bool needContentPack = false;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        if (!item->scene()) {
            m_view->canvasScene()->addItem(item);
        }
        m_view->applyItemModeFlags(item);
        // Image-mode crop may have updated the appearance store. Rematerialize
        // only when durable content exists and the live tile is out of date
        // (or blank). Matching applied xform + pixels → no-op, no pack.
        const SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId
            || !m_view->itemWorld().hasDurableAppearance(sid)) {
            continue;
        }
        const WorkspaceItemState st = m_view->sessionAppearanceValue(sid);
        if (!SessionAppearance::hasContentAppearance(st)) {
            continue;
        }
        const ContentXform::Value want = ContentXform::Value::fromState(st);
        const ContentXform::Value applied = m_view->itemAppliedContentXform(item);
        if (m_view->itemHasAppliedContentXform(item)
            && ContentXform::equal(applied, want)
            && item->hasDisplayPixels()) {
            continue;
        }
        m_view->hostDisplayPipeline().rematerializeGalleryItemFromStore(item);
        needContentPack = true;
    }
    {
        const PackOrderView pack = m_view->currentPackOrder();
        m_view->reorderItemsByPaths(pack.paths(), pack.ids());
    }
    // Crop/aspect change in Image mode: one ContentChange pack (preserve scroll).
    // Plain return: enter() runs a light updateDecodeWindow only.
    if (needContentPack) {
        applyLayout(GalleryPackReason::ContentChange);
    }
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
    if (!m_view->hostDisplayPipeline().loadGate().hasPendingWorkspacePaths()
        && !m_view->liveItems().isEmpty()) {
        m_pendingRestore = false;
        // Keep m_haveViewCenter / m_haveScroll until ExplicitLayout or a settle
        // clear. Clearing them here made returnToGallery's post-geometry
        // reassert a no-op after refreshScrollBarGeometry shifted the view —
        // Gallery→Image→Gallery forgot the scroll position.
    }
}

void GalleryController::reassertViewport()
{
    if (!m_view->isGalleryMode()) {
        return;
    }
    // Prefer scene centre: scrollbar pixel values shift when AlwaysOn↔AsNeeded
    // (or one bar vs two) changes the viewport size between leave and return.
    // Snapshot comment already notes centre is robust across bar policy; pixel
    // restore was leaving Gallery a bar-width off-centre after Image return.
    // Scroll pixels remain a fallback when centre was never captured.
    if (m_haveViewCenter) {
        m_view->centerOn(m_viewCenter);
        return;
    }
    if (m_haveScroll) {
        if (m_view->horizontalScrollBar()) {
            m_view->horizontalScrollBar()->setValue(m_scrollH);
        }
        if (m_view->verticalScrollBar()) {
            m_view->verticalScrollBar()->setValue(m_scrollV);
        }
    }
}


void GalleryController::onLeave(int nextMode)
{
    const auto next = static_cast<ImageView::ViewMode>(nextMode);
    clearChrome();
    m_view->setDragMode(QGraphicsView::NoDrag);
    // Stop deferred packs immediately — a pending 0ms debounce after
    // scrollbar/thumb resize must not re-enter applyLayout while we tear down.
    stopLayoutDebounceTimer();
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

bool GalleryController::returnFromImage(int layoutMode, const QString &focusPath,
                                        SessionImageId focusId)
{
    // Arm restore before enter/applyLayout so packs re-centre on the
    // snapshotted scene point (flags preserved across Gallery→Image leave).
    // Stash non-empty ⇒ enter() will restore the same ImageItem* cells; the
    // shell must not run populateGalleryCanvas / setWorkspacePaths (that was
    // re-walking the full session and could re-arm size work).
    const bool willRestoreStash = !m_stashedItems.isEmpty();
    restoreViewport(focusPath, focusId);
    // Central leave → setActiveMode → enter (MODE_OWNERSHIP). enterGallery
    // routes through setViewMode when not already Gallery so previous=Image is
    // explicit and Gallery stash restore cannot race a stale mode flag.
    auto layout = static_cast<LayoutMode>(layoutMode);
    if (layout == LayoutMode::FreeForm) {
        layout = LayoutMode::Masonry;
    }
    enterGallery(layout);
    applyPendingRestore();
    return willRestoreStash;
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
        applyLayout(GalleryPackReason::EnterGallery);
    } else if (restoredStash) {
        // Warm Image↔Gallery: same ImageItem* cells already carry pack poses,
        // underlays, and tile bags. applyLayout(EnterGallery) re-packed the
        // whole session and ran a full decode window (tile re-issue) — that was
        // the bulk of the “reload” after return. Only fill blanks / tile gaps.
        //
        // Re-apply the planned pack sceneRect (prepareCanvas cleared it). Without
        // this, a residual view override or null sceneRect limits scroll to the
        // live-window bounding box and the overview looks off-centre.
        if (m_virtualSceneBounds.isValid() && m_view->canvasScene()) {
            m_view->setSceneRect(QRectF());
            if (m_view->canvasScene()->sceneRect() != m_virtualSceneBounds) {
                m_view->canvasScene()->setSceneRect(m_virtualSceneBounds);
            }
        }
        // prepareCanvas zeroed scroll; re-apply leave camera now that sceneRect
        // is valid again (returnFromImage also reasserts after enter returns).
        if (m_pendingRestore) {
            applyPendingRestore();
        } else if (m_haveViewCenter || m_haveScroll) {
            reassertViewport();
        }
        updateDecodeWindow();
    }
    // Cold enter (no stash): first pack is owned by setWorkspacePaths /
    // finishGallerySizeResolve after populateGalleryCanvas.

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
    m_view->hostImage().releaseStickyZoom();
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
        m_layout.currentMode() == LayoutMode::SideBySide
        || m_layout.currentMode() == LayoutMode::MasonryRows
        || m_layout.currentMode() == LayoutMode::MasonryRowsFill;

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
        emitItemFocus(hit);
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
            emitItemFocus(hit);
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
            emitItemFocus(hit);
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
            emitItemFocus(hit);
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
}




bool GalleryController::tryMouseDoubleClick(QMouseEvent *event)
{
    // Prefer SessionImageId so duplicate paths open the correct session row.
    if (!m_view->isGalleryMode() || event->button() != Qt::LeftButton) {
        return false;
    }
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        event->accept();
        return true;
    }
    const QPointF scenePos = m_view->mapToScene(event->pos());
    for (QGraphicsItem *gi : scene->items(scenePos)) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            emitItemOpenInImageMode(item);
            event->accept();
            return true;
        }
    }
    event->accept();
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
            emitItemOpenInImageMode(item);
            event->accept();
            return true;
        }
        return false;
    }

    if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
        ImageItem *item = (event->key() == Qt::Key_Home)
                              ? m_view->liveItems().first()
                              : m_view->liveItems().last();
        focusItem(item);
        emitItemFocus(item);
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
    focusItem(best);
    emitItemFocus(best);
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

void GalleryController::onViewResized()
{
    // Never repack from resize (session delete looked like auto-layout).
    // Still refresh the decode window: open often packs at 0×0, soft arrives
    // into ImageCache, and without this pulse cells stay blank until F5/relayout.
    if (QWidget *vp = m_view->viewport()) {
        if (vp->width() > 1 && vp->height() > 1) {
            scheduleDecodeWindowRefresh(GalleryDecode::kDecodeWindowRearmMs);
        }
    }
}

void GalleryController::onViewportLeave()
{
    if (!hoverPath().isEmpty()) {
        clearHoverPath();
        if (QWidget *vp = m_view->viewport()) {
            vp->update();
        }
    }
}

void GalleryController::onContentAppearancePropagated()
{
    // Aspect / crop may change pack cell size — debounce packs concurrent
    // multi-select rotates into one layout pass.
    requestDebouncedPack(GalleryPackReason::ContentChange);
}

void GalleryController::onContentAppearanceReset()
{
    applyLayout(GalleryPackReason::ContentChange);
    // Soft state was reset; kick the ladder for visible tiles.
    updateDecodeWindow();
}

void GalleryController::paintSelectionFrames(QPainter *painter, const QRectF &exposed) const
{
    if (!painter || !m_view) {
        return;
    }
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    const QList<QGraphicsItem *> selected = scene->selectedItems();
    if (selected.isEmpty()) {
        return;
    }
    painter->save();
    painter->setBrush(Qt::NoBrush);
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Double ring so selection reads on light and dark tiles (single cyan was
    // easy to lose). Cosmetic widths = device pixels under any zoom.
    QPen outer(QColor(0, 0, 0, 200));
    outer.setCosmetic(true);
    outer.setWidthF(7.0);
    QPen inner(QColor(0, 200, 255, 255));
    inner.setCosmetic(true);
    inner.setWidthF(3.0);
    // Soft wash so the cell is obviously "in" the selection set.
    const QColor wash(0, 180, 255, 36);
    for (QGraphicsItem *gi : selected) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->isInteractive()) {
            continue;
        }
        // Same rect the content paint uses (gallery clip when Grid-Crop).
        const QRectF local = item->displayContentRect();
        const QPolygonF scenePoly = item->mapToScene(local);
        const QRectF bounds = scenePoly.boundingRect();
        if (!exposed.isNull() && !exposed.intersects(bounds)) {
            continue;
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(wash);
        painter->drawPolygon(scenePoly);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(outer);
        painter->drawPolygon(scenePoly);
        painter->setPen(inner);
        painter->drawPolygon(scenePoly);
    }
    painter->restore();
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
        const int before = item->displayPixelLongEdge();
        const bool had = item->hasDisplayPixels();
        if (!m_view->hostDisplayPipeline().tryInstallGalleryUnderlay(item)) {
            continue;
        }
        const int after = item->displayPixelLongEdge();
        if (after <= before && had) {
            continue;
        }
        GalleryDecodeState &st = m_decodeBook.state(item->path());
        st.have = GalleryDecode::maxHave(st.have, after);
        item->update();
        ++installed;
    }
    return installed;
}

void GalleryController::updateDecodeWindow()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("updateGalleryDecodeWindow");
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

        GalleryDecodeState &st = m_decodeBook.state(path);
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
    GUI_BUDGET("GalleryController::applyLayout");
    // User-driven relayout: drop Image→Gallery restore snapshots so a deferred
    // reassert cannot snap a new layout back to the leave camera.
    // EnterGallery still keeps the leave camera when returning from Image
    // (m_haveViewCenter) even after pendingRestore was cleared post-install.
    if (reason == GalleryPackReason::ExplicitLayout) {
        m_pendingRestore = false;
        m_haveScroll = false;
        m_haveViewCenter = false;
    } else if (reason == GalleryPackReason::EnterGallery && !m_pendingRestore
               && !m_haveViewCenter) {
        m_pendingRestore = false;
        m_haveScroll = false;
        m_haveViewCenter = false;
    }
    if (m_layoutApply.active()) {
        return;
    }
    // Size-first open: while the gate is active, the virtual plan holds every
    // definitive/failed row (sparse — holes skipped). ContentChange packs that
    // set so cells are not stuck at the origin. Full session pack still runs
    // on gate complete (EnterGallery).
    if (m_view->hostGallerySizeResolve().active()
        && reason != GalleryPackReason::ContentChange
        && reason != GalleryPackReason::EnterGallery
        && reason != GalleryPackReason::Reload) {
        return;
    }
    // Packaged packing is Gallery-only; never rearrange Workspace free-form items.
    // Virtualized: path order may be full while liveItems is still empty.
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty() || m_layout.isFreeForm()) {
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

    LayoutApplyGuard::Scoped layoutApplyScope(&m_layoutApply);

    // Packaged layouts use view pixels as scene units so images scale to the window
    m_view->resetTransform();
    // Do not origin-jump when returning from Image (leave camera still armed).
    if (!m_pendingRestore && !preserveView && !m_haveViewCenter) {
        m_view->centerOn(0, 0);
    }

    // Measure with both scrollbar gutters reserved (AlwaysOn). AsNeeded would
    // pack to the full client, then one bar shrinks the viewport and the other
    // axis overshoots — dual scrollbars. hostLayoutApply is active so policy
    // changes do not re-enter pack via resizeEvent.
    PackViewportGuard packVp(m_view);
    const qreal margin = GalleryLayout::Params::kDefaultMargin;
    const qreal gap = GalleryLayout::Params::kDefaultGap;
    const qreal availW = GalleryPackFit::packAvailAxis(packVp.width(), margin);
    const qreal availH = GalleryPackFit::packAvailAxis(packVp.height(), margin);

    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_layout.masonryColumnsValue();
    params.gridColumns = m_layout.gridColumnsValue();
    params.masonryRows = m_layout.masonryRowsValue();
    params.mode = GalleryPackFit::modeFromLayoutMode(m_layout.currentMode());

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

    QRectF bounds = m_virtualSceneBounds.isValid()
        ? m_virtualSceneBounds
        : ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(), margin);
    const GalleryLayout::Mode packMode =
        GalleryPackFit::modeFromLayoutMode(m_layout.currentMode());
    bounds = GalleryPackFit::clampSceneRectToPack(bounds, packMode, availW, availH, margin);
    // Scene is sole authority; drop any QGraphicsView-level override first.
    m_view->setSceneRect(QRectF());
    if (m_view->canvasScene()->sceneRect() != bounds) {
        m_view->canvasScene()->setSceneRect(bounds);
    }
    // Restore AsNeeded/Off only after sceneRect is clamped to the measured pack
    // size. Still under hostLayoutApply so policy-driven resize does not repack.
    packVp.restore();
    // Policy toggle can leave AsNeeded bars with a stale range until forced.
    if (m_pendingRestore || preserveView) {
        m_view->refreshScrollBarGeometry();
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
    // Re-apply scene centre after bar policy settles — returning from Image.
    // Prefer leave-camera reassert over pending-only (pending may already be false).
    if (m_pendingRestore) {
        applyPendingRestore();
    } else if (m_haveViewCenter || m_haveScroll) {
        reassertViewport();
    }
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
    GUI_BUDGET("GalleryController::ensurePlaceholders");
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty()) {
        return false;
    }
    // Size gate: plan every definitive/failed row (sparse; unresolved skipped).
    rebuildVirtualPlan();
    syncVirtualWindow();
    return false;
}

void GalleryController::rebuildVirtualPlan()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("GalleryController::rebuildVirtualPlan");
    m_virtualSlots.clear();
    m_virtualSceneBounds = QRectF();
    if (!m_view->isGalleryMode() || m_view->pathOrderIsEmpty()
        || m_layout.isFreeForm()) {
        return;
    }

    const PackOrderView pack = m_view->currentPackOrder();
    const ImageSizeBook &book = m_view->hostSizeBook();

    QVector<QSizeF> sizes;
    sizes.reserve(pack.size());
    m_virtualSlots.reserve(pack.size());

    for (int i = 0; i < pack.size(); ++i) {
        const QString path = pack.pathAt(i);
        const SessionImageId sid = pack.idAt(i);
        // Never invent square stand-ins (1000²) for pack — that made every cell
        // square and galleryClipLocal cropped real content into the wrong aspect.
        // Only definitive probe/fail sizes participate in the plan.
        if (!book.hasDefinitive(path) && !book.isFailed(path)) {
            if (layoutNeedsAllSizes(m_layout.currentMode())) {
                // Fill layouts need the full aspect set — empty plan until done.
                m_virtualSlots.clear();
                return;
            }
            // Sparse progressive: skip unresolved holes. SizeReply is out of
            // order (bounded workers); an ordered prefix stopped the plan at the
            // first gap so ~dozens of cells appeared then nothing until the
            // whole gate finished. Include every definitive/failed row.
            continue;
        }
        // Orient-aware layout size without Store I/O (allowStoreAppearance=false).
        QSize lay = m_view->contentLayoutSize(path, sid, /*allowStoreAppearance=*/false);
        if (!isPositiveSize(lay) || lay.width() <= 1) {
            lay = book.known(path);
        }
        // Still nothing usable — skip row rather than standInNeutral square.
        if (!isPositiveSize(lay) || lay.width() <= 1) {
            continue;
        }
        // Reject provisional-shaped geometry if it slipped into known().
        if (book.isProvisional(path) && !book.isFailed(path)) {
            continue;
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

        // Same gutter reservation as applyLayout — virtual plan must not pack to a
    // wider/taller client than live items (AsNeeded viewport without bars).
    PackViewportGuard packVp(m_view);
    const qreal margin = GalleryLayout::Params::kDefaultMargin;
    const qreal gap = GalleryLayout::Params::kDefaultGap;
    const int vpW = packVp.width() > 0 ? packVp.width() : 800;
    const int vpH = packVp.height() > 0 ? packVp.height() : 600;
    const qreal availW = GalleryPackFit::packAvailAxis(vpW, margin);
    const qreal availH = GalleryPackFit::packAvailAxis(vpH, margin);


    GalleryLayout::Params params;
    params.margin = margin;
    params.gap = gap;
    params.availW = availW;
    params.availH = availH;
    params.masonryColumns = m_layout.masonryColumnsValue();
    params.gridColumns = m_layout.gridColumnsValue();
    params.masonryRows = m_layout.masonryRowsValue();
    params.mode = GalleryPackFit::modeFromLayoutMode(m_layout.currentMode());

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
    m_virtualSceneBounds = GalleryPackFit::clampSceneRectToPack(
        m_virtualSceneBounds, params.mode, availW, availH, margin);
    ++m_virtualPlanGeneration;
}


void GalleryController::syncVirtualWindow()
{
    ASSERT_GUI_THREAD();
    GUI_BUDGET("GalleryController::syncVirtualWindow");
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
        // Before first show: use the full planned scene so the top of the
        // session materializes; viewport size will refine on the next scroll.
        sceneVis = m_virtualSceneBounds;
    }

    // Live set = slots intersecting the (overscanned) viewport. No arbitrary
    // hard cap — dense packs must materialize every visible cell.
    QSet<int> want;
    for (int i = 0; i < m_virtualSlots.size(); ++i) {
        if (m_virtualSlots.at(i).bounds.intersects(sceneVis)) {
            want.insert(i);
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
    QElapsedTimer sliceTimer;
    sliceTimer.start();
    constexpr qint64 kVirtualWindowSliceMs = 12;
    bool needAnotherSlice = false;
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
            if (sliceTimer.elapsed() >= kVirtualWindowSliceMs) {
                needAnotherSlice = true;
                continue;
            }
            // Lazy size probe for on-screen rows not covered by the open prefix.
            if (!slot.path.isEmpty()
                && !m_view->hostSizeBook().hasDefinitive(slot.path)
                && !m_view->hostSizeBook().isFailed(slot.path)) {
                ThumtooCache::scheduleProbe(slot.path);
            }
            const QSize sz = slot.layoutSize.toSize();
            // Slot layoutSize is definitive-only (rebuildVirtualPlan); never
            // invent a square stand-in here.
            if (!isPositiveSize(sz)) {
                continue;
            }
            item = m_view->hostDisplayPipeline().createPlaceholderItem(slot.path, sz);
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

        // Single underlay path (SizeReply → ImageCache → SoftPreview).
        if (item) {
            m_view->hostDisplayPipeline().tryInstallGalleryUnderlay(item);
        }
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
        m_view->setSceneRect(QRectF());
        if (m_view->canvasScene()->sceneRect() != m_virtualSceneBounds) {
            m_view->canvasScene()->setSceneRect(m_virtualSceneBounds);
        }
    }
    if (needAnotherSlice) {
        QTimer::singleShot(0, m_view, [this]() {
            if (m_view && m_view->isGalleryMode()) {
                syncVirtualWindow();
            }
        });
    }
}

void GalleryController::paintVirtualPlaceholders(QPainter *painter, const QRectF &exposed) const
{
    GUI_BUDGET("GalleryController::paintVirtualPlaceholders");
    if (!painter || !m_view || !m_view->isGalleryMode() || m_virtualSlots.isEmpty()) {
        return;
    }
    // Underlay from ImageCache when SizeReply seeded it; else darker offline
    // chrome. Live ImageItems paint on top when present.
    painter->save();
    for (const VirtualSlot &slot : m_virtualSlots) {
        if (!slot.bounds.intersects(exposed)) {
            continue;
        }
        QImage under;
        if (!slot.path.isEmpty()) {
            under = ImageCache::get(slot.path);
        }
        if (!under.isNull()) {
            // Draw scaled underlay into the plan cell (spec: virtualized LQIP).
            const QRectF &r = slot.bounds;
            const QSize target(qMax(1, int(r.width())), qMax(1, int(r.height())));
            const QImage scaled = under.scaled(target, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
            const qreal x = r.x() + (r.width() - scaled.width()) * 0.5;
            const qreal y = r.y() + (r.height() - scaled.height()) * 0.5;
            painter->fillRect(r, QColor(28, 28, 30));
            painter->drawImage(QPointF(x, y), scaled);
        } else {
            ItemFrameGeometry::paintVirtualOfflinePlaceholder(painter, slot.bounds);
        }
    }
    painter->restore();
}


void GalleryController::scheduleSizeGatePlanRefresh()
{
    ASSERT_GUI_THREAD();
    if (!m_view || !m_view->isGalleryMode()) {
        return;
    }
    if (!m_sizeGatePlanTimer) {
        m_sizeGatePlanTimer = new QTimer(m_view);
        m_sizeGatePlanTimer->setSingleShot(true);
        QObject::connect(m_sizeGatePlanTimer, &QTimer::timeout, m_view, [this]() {
            if (!m_view || !m_view->isGalleryMode()) {
                return;
            }
            if (!m_view->hostGallerySizeResolve().active()) {
                return;
            }
            // Plan + visible window only — no full applyLayout (that froze opens).
            (void)ensurePlaceholders();
            if (m_view->viewport()) {
                m_view->viewport()->update();
            }
        });
    }
    m_sizeGatePlanTimer->setInterval(40);
    m_sizeGatePlanTimer->start();
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
            m_view->hostShell().clearCentreProgress();
        }
        return;
    }
    // Only show when a meaningful fraction is still blank (avoid flicker on one
    // cell). Clear a prior chip so we do not leave "Loading tiles…" stuck at
    // 1 residual blank forever.
    if (blank < 2 && total > 8) {
        if (m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Loading tiles"))
            || m_view->hostCentreProgress().matchesTitlePrefix(m_view->tr("Improving previews"))) {
            m_view->hostShell().clearCentreProgress();
        }
        return;
    }
    const QString detail =
        m_view->tr("%1 / %2 on-screen cells ready").arg(total - blank).arg(total);
    m_view->hostShell().setCentreProgress(m_view->tr("Loading tiles…"), detail);
}

void GalleryController::setGridColumns(int columns)
{
    const int before = m_layout.gridColumnsValue();
    m_layout.setGridColumns(columns);
    if (m_layout.gridColumns == before) {
        return;
    }
    if (m_view->isGalleryMode()
        && (layoutIsGridFamily(m_layout.currentMode())
            || layoutIsFlowFamily(m_layout.currentMode())
            || m_layout.currentMode() == LayoutMode::Facing)) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void GalleryController::setMasonryColumns(int columns)
{
    const int before = m_layout.masonryColumnsValue();
    m_layout.setMasonryColumns(columns);
    if (m_layout.masonryColumns == before) {
        return;
    }
    if ((layoutIsMasonryColumns(m_layout.currentMode()))
        && !m_view->liveItems().isEmpty()) {
        applyLayout(GalleryPackReason::ExplicitLayout);
    }
}

void GalleryController::setMasonryRows(int rows)
{
    const int before = m_layout.masonryRowsValue();
    m_layout.setMasonryRows(rows);
    if (m_layout.masonryRows == before) {
        return;
    }
    if ((m_layout.currentMode() == LayoutMode::MasonryRows || m_layout.currentMode() == LayoutMode::MasonryRowsFill)
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
    // Clear QGraphicsView-level sceneRect override (Image mode used to set it
    // via setSceneRect on the view). Scene-only setSceneRect does not remove
    // that override — Gallery return then scrolled a one-image rect.
    m_view->setSceneRect(QRectF());
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
        enterGallery(mode);
        return;
    }

    if (m_layout.isFreeForm() && mode != LayoutMode::FreeForm) {
        m_view->hostWorkspace().snapshotFreeFormStates();
    }

    m_layout.setMode(mode);
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
        m_view->hostDisplayPipeline().hostClearDecodedPixels(item);
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
    m_view->hostHud().showFlash(ImageView::tr("Reload"), ImageView::tr("Gallery"), [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
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
        m_view->hostDisplayPipeline().hostClearDecodedPixels(item);
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
    m_view->hostHud().showFlash(ImageView::tr("Hard reload"), detail, [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });

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
            m_view->hostHud().showFlash(ImageView::tr("Hard reload"), ImageView::tr("%1 Store tiles removed").arg(*tileTotal), [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
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
        m_relayoutSuppress.push(true);
        stopLayoutDebounceTimer();
    } else if (m_relayoutSuppress.active()) {
        m_relayoutSuppress.push(false);
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

void GalleryController::stopLayoutDebounceTimer()
{
    if (m_layoutDebounceTimer) {
        m_layoutDebounceTimer->stop();
    }
}

void GalleryController::requestDebouncedPack(GalleryPackReason reason)
{
    const bool progressive = m_sizeResolve.active();
    m_layoutDebounce.arm(reason, progressive);
    if (!m_layoutDebounceTimer) {
        if (progressive) {
            ensurePlaceholders();
        }
        applyLayout(reason);
        return;
    }
    // Continuous sizeReady restarts a quiet-period timer and can leave the
    // sized prefix unlaid-out for the whole probe stream. Force a pack when
    // the progressive arm has been pending past the max wait.
    if (progressive && m_layoutDebounce.progressiveMaxWaitExceeded()) {
        m_layoutDebounceTimer->stop();
        GalleryPackReason r = reason;
        if (m_layoutDebounce.take(&r)) {
            ensurePlaceholders();
            applyLayout(r);
        }
        return;
    }
    m_layoutDebounceTimer->setInterval(
        progressive ? LayoutDebounce::kProgressiveIntervalMs
                    : LayoutDebounce::kIntervalMs);
    m_layoutDebounceTimer->start();
}

void GalleryController::startDecodeWatchdog()
{
    if (!m_decodeWatchdogTimer) {
        m_decodeWatchdogTimer = new QTimer(m_view);
        m_decodeWatchdogTimer->setInterval(GalleryDecode::kWatchdogIntervalMs);
        QObject::connect(m_decodeWatchdogTimer, &QTimer::timeout, m_view, [this]() {
            if (m_view->isGalleryMode()) {
                decodeWatchdogTick();
            }
        });
    }
    m_decodeWatchdogTimer->start();
}

void GalleryController::stopDecodeWatchdog()
{
    if (m_decodeWatchdogTimer) {
        m_decodeWatchdogTimer->stop();
    }
}

void GalleryController::countLoadingTileStats(int *blankOut, int *weakOut) const
{
    int blank = 0;
    int weak = 0;
    if (!m_view) {
        if (blankOut) {
            *blankOut = 0;
        }
        if (weakOut) {
            *weakOut = 0;
        }
        return;
    }
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        if (!item->hasDisplayPixels()) {
            ++blank;
        } else if (item->displayPixelLongEdge() > 0
                   && item->displayPixelLongEdge() <= DisplayQuality::kLqipMaxEdge) {
            ++weak;
        }
    }
    if (blankOut) {
        *blankOut = blank;
    }
    if (weakOut) {
        *weakOut = weak;
    }
}

void GalleryController::countDebugPixelMix(int *blankOut, int *lqipOut, int *softOut,
                                           int *higherOut, int *climbingOut) const
{
    int blank = 0, lqip = 0, soft = 0, better = 0, climb = 0;
    if (m_view) {
        for (ImageItem *ii : m_view->liveItems()) {
            if (!ii || ii->path().isEmpty()) {
                continue;
            }
            const int e = ii->displayPixelLongEdge();
            if (!ii->hasDisplayPixels() || e <= 0) {
                ++blank;
            } else {
                switch (DisplayQuality::tierOf(e)) {
                case DisplayQuality::Tier::Lqip:
                    ++lqip;
                    break;
                case DisplayQuality::Tier::Soft:
                    ++soft;
                    break;
                default:
                    ++better;
                    break;
                }
            }
            if (const GalleryDecodeState *sit = m_decodeBook.get(ii->path())) {
                if (sit->inflight > 0) {
                    ++climb;
                }
            }
        }
    }
    if (blankOut) {
        *blankOut = blank;
    }
    if (lqipOut) {
        *lqipOut = lqip;
    }
    if (softOut) {
        *softOut = soft;
    }
    if (higherOut) {
        *higherOut = better;
    }
    if (climbingOut) {
        *climbingOut = climb;
    }
}

int GalleryController::uniqueBlankPathCount() const
{
    if (!m_view) {
        return 0;
    }
    QSet<QString> blankPaths;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        if (!item->hasDisplayPixels()) {
            blankPaths.insert(item->path());
        }
    }
    return blankPaths.size();
}
