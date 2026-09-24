// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspace/workspacecontroller.h"
#include "image/toolpolicy.h"
#include <QGraphicsView>
#include "item/itemcomponents.h"
#include <memory>
#include <QSet>
#include <QFileInfo>
#include "host/thumtoocache.h"
#include "display/imagecache.h"
#include "display/displayquality.h"
#include "view/viewtransform.h"
#include "imageview.h"
#include "session/sessionbindbook.h"
#include <QUndoStack>
#include "gallery/gallerypackfit.h"
#include "gallery/gallerylayout.h"
#include "imageitem.h"
#include "item/placementlinear.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsScene>
#include "imageitem.h"
#include "item/placementlinear.h"
#include "host/imageloader.h"
#include "session/sessionappearance.h"

#include <QScrollBar>
#include <QtMath>

WorkspaceController::WorkspaceController(ImageView *view)
    : m_view(view)
{
}

void WorkspaceController::snapshot()
{
    // Re-entrant leave (Workspace onLeave then Gallery enter while mode is still
    // Workspace) must not wipe a durable snapshot already taken from live tiles.
    if (m_view->liveItems().isEmpty()) {
        return;
    }
    m_savedItems.clear();
    for (ImageItem *item : m_view->liveItems()) {
        WorkspaceItemState s = m_view->freezeItemAppearance(item);
        // Bound: durable content already on ItemWorld (flushApplied ran before
        // onLeave). freezeItemAppearance may carry live color lag into
        // colorAdjust — setAppearance would promote lag into durable Color
        // (ECS_GUI_BYPASSES #5). Snapshot slots are Placement-only for bound
        // (2212/2213); restore re-reads content via sessionAppearanceValue.
        // Unbound: path map is the sole store (even when sessionIndex is -1).
        if (s.sessionId != kInvalidSessionImageId) {
            s = SessionAppearance::clearedContentOps(s);
            m_view->itemWorld().setPlacement(
                s.sessionId, ItemComponents::placementFromState(s));
        } else if (!s.path.isEmpty()) {
            m_view->itemWorld().setPathState(s.path, s);
        }
        m_savedItems.append(s);
    }
    // Durable view backup when the live stash is later discarded (e.g. Gallery).
    m_savedViewTransform = m_view->transform();
    m_savedViewCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    m_hasSavedView = true;
}

void WorkspaceController::restore()
{
    // clearWorkspace() also clears m_savedItems — that wiped this durable
    // snapshot and left Gallery→Workspace empty. Only drop live tiles.
    m_view->clearLiveCanvas();
    m_view->hostDisplayPipeline().loadGate().clearPendingWorkspacePaths();
    // Merge live store into the durable snapshot so Image-mode edits survive
    // a full rebuild. Bound: appearance by id (crop never from path). Unbound:
    // path map may hold crop + orient.
    for (WorkspaceItemState &slot : m_savedItems) {
        if (slot.sessionId != kInvalidSessionImageId) {
            if (m_view->itemWorld().hasDurableAppearance(slot.sessionId)) {
                // Pose from snapshot (Placement bridge); content only from
                // ItemWorld (ECS_GUI_BYPASSES #5 / 2213–2214).
                const ItemComponents::Placement pose =
                    ItemComponents::placementFromState(slot);
                const int sessionIndex = slot.sessionIndex;
                const QString path = slot.path;
                const SessionImageId sid = slot.sessionId;
                slot = m_view->sessionAppearanceValue(sid);
                ItemComponents::applyPlacementToState(slot, pose);
                slot.sessionIndex = sessionIndex;
                slot.path = path;
                slot.sessionId = sid;
                continue;
            }
            // Bound but no appearance yet: leave identity; pipeline XDG seed
            // fills sparse on decode (path book no longer holds bound content).
            continue;
        }
        const WorkspaceItemState *it = m_view->itemWorld().getPathState(slot.path);
        if (!it) {
            continue;
        }
        // Unbound path row: require matching list index when both sides have one.
        if (slot.sessionIndex >= 0 && it->sessionIndex >= 0
            && slot.sessionIndex != it->sessionIndex) {
            continue;
        }
        slot.hasCrop = it->hasCrop;
        slot.cropRect = it->cropRect;
        slot.hFlip = it->hFlip;
        slot.vFlip = it->vFlip;
        slot.contentQuarterTurns = it->contentQuarterTurns;
        slot.contentHFlip = it->contentHFlip;
        slot.contentVFlip = it->contentVFlip;
    }
    // AUDIT M27: queue every saved state (including duplicate paths) then load.
    m_view->hostDisplayPipeline().loadGate().setPendingRestoreStates(m_savedItems);
    for (const WorkspaceItemState &state : m_savedItems) {
        // Bound content lives on appearance by id (already written in snapshot).
        // Path map only for unbound — avoid last-duplicate crop leak on shared path.
        if (state.sessionId == kInvalidSessionImageId && !state.path.isEmpty()) {
            m_view->itemWorld().setPathState(state.path, state);
        }
        m_view->scheduleRestoreLoad(state.path);
    }
    m_view->hostFraming().clearFitFill();
    // Apply zoom before scene-rect expansion; pan after range is valid.
    if (m_hasSavedView) {
        m_view->setTransform(m_savedViewTransform);
    }
    updateSceneRect();
    if (m_hasSavedView) {
        m_view->centerOn(m_savedViewCenter);
        m_hasSavedView = false;
    }
    emit m_view->statusChanged();
}

void WorkspaceController::discardStash()
{
    // Snapshot + clear first (re-entrant / duplicate-safe). Not destroyCanvasItem:
    // off-list after take; must not clear undo or path-book from abandoned tiles.
    QList<ImageItem *> doomed = m_stashedItems;
    m_stashedItems.clear();
    m_hasStashedView = false;
    QSet<ImageItem *> seen;
    for (ImageItem *item : doomed) {
        if (!item || seen.contains(item)) {
            continue;
        }
        seen.insert(item);
        // Stage 2: bags + display surface before delete.
        m_view->hostDisplayPipeline().dropItemTileLodSession(item);
        m_view->hostDisplayPipeline().releaseTileBag(item);
        m_view->hostDisplayPipeline().unregisterItemDisplaySurface(item);
        if (QGraphicsScene *sc = item->scene()) {
            sc->removeItem(item);
        }
        delete item;
    }
}

void WorkspaceController::clearDurableSnapshot()
{
    m_savedItems.clear();
    m_hasSavedView = false;
    m_freeFormById.clear();
    m_freeFormByPath.clear();
    m_hasFreeFormViewTransform = false;
}

void WorkspaceController::stashItems()
{
    // Live canvas already empty: keep any existing stash (Workspace onLeave
    // already moved tiles off-scene; Gallery enter must not discard them).
    if (m_view->liveItems().isEmpty()) {
        return;
    }
    // Replace any previous workspace stash (e.g. nested mode switches).
    discardStash();
    // Zoom lives in the view matrix; pan lives in scrollbars — capture both.
    // Scene centre is robust across sceneRect rebuilds (same idea as Gallery).
    m_stashedViewTransform = m_view->transform();
    m_stashedViewCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    m_hasStashedView = true;
    m_stashedItems = m_view->liveItems();
    clearInteractionState();
    for (ImageItem *item : m_stashedItems) {
        if (!item) {
            continue;
        }
        // Keep selection flags on the item for restore; only detach from scene.
        if (item->scene()) {
            item->scene()->removeItem(item);
        }
    }
    m_view->liveItems().clear();
}

void WorkspaceController::restoreStashedItems()
{
    if (m_stashedItems.isEmpty()) {
        return;
    }
    // Drop Image-mode canvas (single tile) without touching the stash.
    // Never QGraphicsScene::clear() — stashed tiles may still be scene-parented
    // if removeItem was skipped; clear() would delete them and leave dangling
    // pointers in m_stashedItems / liveItems.
    QSet<ImageItem *> keep;
    for (ImageItem *item : m_stashedItems) {
        if (item) {
            keep.insert(item);
        }
    }
    while (!m_view->liveItems().isEmpty()) {
        ImageItem *item = m_view->liveItems().last();
        if (keep.contains(item)) {
            // Should not happen (stash owns these); detach from live only.
            m_view->liveItems().removeAll(item);
            continue;
        }
        m_view->destroyCanvasItem(item);
    }
    if (QGraphicsScene *sc = m_view->canvasScene()) {
        sc->blockSignals(true);
        for (QGraphicsItem *gi : sc->items()) {
            if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                if (keep.contains(ii)) {
                    sc->removeItem(ii);
                    continue;
                }
            }
            // Residual non-stashed graphics (page guide is not ImageItem).
            if (qgraphicsitem_cast<ImageItem *>(gi)) {
                sc->removeItem(gi);
                delete gi;
            }
        }
        sc->blockSignals(false);
    }
    m_view->liveItems() = m_stashedItems;
    m_stashedItems.clear();
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        if (!item->scene()) {
            m_view->canvasScene()->addItem(item);
        }
        m_view->applyItemModeFlags(item);
        // Prefer per-session-slot appearance (value copy for this slot).
        // Fall back to path map only for the sole instance of a path.
        WorkspaceItemState app;
        bool haveApp = false;
        if (item->sessionId() != kInvalidSessionImageId
            && m_view->itemWorld().hasDurableAppearance(item->sessionId())) {
            app = m_view->sessionAppearanceValue(item->sessionId());
            haveApp = true;
        }
        // Unbound only: path map may hold content. Bound without durable row
        // waits for XDG seed on next materialize (path content stripped — 2069).
        if (!haveApp && item->sessionId() == kInvalidSessionImageId) {
            int samePath = 0;
            for (ImageItem *peer : m_view->liveItems()) {
                if (peer && peer->path() == item->path()) {
                    ++samePath;
                }
            }
            if (samePath == 1) {
                if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(item->path())) {
                    app = *st;
                    haveApp = true;
                }
            }
        }
        if (!haveApp) {
            continue;
        }
        // Stash is presentation-only: drop applied fingerprint if ItemWorld
        // advanced while this tile was off-canvas (ECS_GUI_BYPASSES #6 / 2196).
        m_view->hostDisplayPipeline().clearStaleAppliedFingerprintIfNeeded(item);
        // content ops require full on-disk host for crop / content
        // flips / quarter-turns. Stashed tiles often already hold baked crop
        // pixels (peer sync while in Image mode). Reloading only on size
        // mismatch alternated wrong/correct every Workspace↔Image cycle:
        // match → skip reload → double-crop → mismatch → reload → OK → …
        const bool needsFullSource = app.hasCrop || app.contentQuarterTurns != 0
            || app.contentHFlip || app.contentVFlip
            || item->sourceImage().isNull();
        if (needsFullSource) {
            const QImage full = m_view->hostDisplayPipeline().fullRasterForEdit(item->path());
            if (!full.isNull()) {
                // Raw on-disk reload → single gate (avoids double-bake on cycle).
                m_view->hostDisplayPipeline().installDisplayPixels(
                    item, full, SessionAppearance::PixelKind::FullSource,
                    item->sessionId());
            } else {
                // No full host: rematerialize from ImageCache soft/host (stand-in
                // + async). Do not chrome-only on multi-MP — cannot
                // bake crop on the GUI and must not claim applied == want.
                m_view->hostDisplayPipeline().rematerializeItemContent(item, app);
            }
        } else {
            // Grade-only (or content already on soft): rematerialize so multi-MP
            // still gets grade via soft stand-in + async. A chrome-only path
            // alone leaves multi-MP grade as chrome-only without a bake.
            m_view->hostDisplayPipeline().rematerializeItemContent(item, app);
        }
    }
    m_view->hostFraming().clearFitFill();
    // Order: zoom → expand sceneRect for the new scale → pan to saved centre.
    // updateWorkspaceSceneRect alone would leave scrollbars at Image-mode zeros.
    if (m_hasStashedView) {
        m_view->setTransform(m_stashedViewTransform);
    }
    updateSceneRect();
    if (m_hasStashedView) {
        m_view->centerOn(m_stashedViewCenter);
        m_hasStashedView = false;
    }
    m_view->viewport()->update();
}

void WorkspaceController::snapshotFreeFormStates()
{
    m_freeFormById.clear();
    m_freeFormByPath.clear();
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        const ItemComponents::Placement pl = item->placement();
        if (item->sessionId() != kInvalidSessionImageId) {
            m_freeFormById.insert(item->sessionId(), pl);
        } else if (!item->path().isEmpty()) {
            m_freeFormByPath.insert(item->path(), pl);
        }
    }
    m_freeFormViewTransform = m_view->transform();
    m_hasFreeFormViewTransform = true;
}

void WorkspaceController::restoreFreeFormStates()
{
    for (ImageItem *item : m_view->liveItems()) {
        if (!item) {
            continue;
        }
        ItemComponents::Placement pl;
        bool found = false;
        if (item->sessionId() != kInvalidSessionImageId) {
            const auto it = m_freeFormById.constFind(item->sessionId());
            if (it != m_freeFormById.constEnd()) {
                pl = *it;
                found = true;
            }
        }
        if (!found && !item->path().isEmpty()) {
            const auto it = m_freeFormByPath.constFind(item->path());
            if (it != m_freeFormByPath.constEnd()) {
                pl = *it;
                found = true;
            }
        }
        if (!found) {
            continue;
        }
        GalleryLayout::applyItemPlacement(item, pl);
        if (item->sessionId() != kInvalidSessionImageId) {
            m_view->itemWorld().setPlacement(item->sessionId(), pl);
        } else if (!item->path().isEmpty()) {
            WorkspaceItemState s;
            if (const WorkspaceItemState *prev = m_view->itemWorld().getPathState(item->path())) {
                s = *prev;
            }
            ItemComponents::applyPlacementToState(s, pl);
            m_view->itemWorld().setPathState(item->path(), s);
        }
    }
    if (m_hasFreeFormViewTransform) {
        m_view->setTransform(m_freeFormViewTransform);
    }
}


void WorkspaceController::onLeave(int nextMode)
{
    const auto next = static_cast<ImageView::ViewMode>(nextMode);
    // Durable placement + appearance backup for rebuild if the live stash is
    // later discarded.
    snapshot();
    if (next == ImageView::ViewMode::Image || next == ImageView::ViewMode::Gallery) {
        // Keep free-form tiles + pixels + view for a fast return to Workspace.
        // Gallery packs from session paths; stashed tiles stay off-scene.
        stashItems();
    }
}

void WorkspaceController::enter(int previousMode)
{
    const auto previous = static_cast<ImageView::ViewMode>(previousMode);
    m_view->setActiveMode(ImageView::ViewMode::Workspace, LayoutMode::FreeForm);
    applyToolDragMode();
    bool keepViewTransform = false;

    // Cross-mode return: prefer live pointer stash (fast, no re-decode) when
    // present; Image enter no longer deletes stashed tiles via scene clear.
    // If stash is empty or reattach leaves no live tiles, fall back to durable
    // m_savedItems via LoadRestore. Do not discardStash before trying reattach.
    if (previous == ImageView::ViewMode::Image
        || previous == ImageView::ViewMode::Gallery) {
        if (!m_stashedItems.isEmpty()) {
            restoreStashedItems();
            keepViewTransform = true;
        }
        if (m_view->liveItems().isEmpty() && !m_savedItems.isEmpty()) {
            discardStash(); // only free residual stash if we must rebuild
            const bool hadSavedView = m_hasSavedView;
            restore();
            keepViewTransform = hadSavedView || keepViewTransform;
        } else if (m_view->liveItems().isEmpty()) {
            m_view->clearLiveCanvas();
            m_view->hostGallery().invalidateDecodes();
            m_view->pathOrderClear();
            m_view->applyModeFlagsToLiveItems();
        }
    } else if (!m_stashedItems.isEmpty()) {
        restoreStashedItems();
        keepViewTransform = true;
    } else if (!m_savedItems.isEmpty()) {
        const bool hadSavedView = m_hasSavedView;
        restore();
        keepViewTransform = hadSavedView;
    } else {
        m_view->clearLiveCanvas();
        m_view->hostGallery().invalidateDecodes();
        m_view->pathOrderClear();
        m_view->applyModeFlagsToLiveItems();
    }
    // Always clear canvas selection on enter — restored stash may keep old
    // selected flags, which MainWindow would mirror onto every filmstrip row.
    if (m_view->canvasScene()) {
        m_view->canvasScene()->clearSelection();
    }
    // Fresh Workspace (no stashed/saved camera): overview scale, not 1:1.
    if (!keepViewTransform) {
        m_view->hostImage().setWorkspaceDefaultViewScale();
    }
    updateSceneRect();
    emit m_view->statusChanged();
}


// --- Workspace select input (Tier 6d) ---

bool WorkspaceController::tryMousePressSelect(QMouseEvent *event)
{
    // Workspace Select tool: item move/select, or click the page guide sheet.
    if (!m_view->isWorkspaceMode() || event->button() != Qt::LeftButton) {
        return false;
    }
        const QPointF scenePos = m_view->mapToScene(event->pos());
        ImageItem *itemHit = nullptr;
        if (m_view->canvasScene()) {
            for (QGraphicsItem *gi : m_view->canvasScene()->items(scenePos)) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    if (ii->isInteractive() && m_view->liveItems().contains(ii)) {
                        itemHit = ii;
                        break;
                    }
                }
            }
        }
        if (!itemHit && m_view->hostPageGuide().isVisible()
            && m_view->pageGuideSceneRect().contains(scenePos)) {
            if (m_view->canvasScene()) {
                m_view->canvasScene()->clearSelection();
            }
            m_view->setPageGuideSelected(true);
            event->accept();
            emit m_view->statusChanged();
            return true;
        }
        m_view->setPageGuideSelected(false);
        m_view->forwardGraphicsViewMousePress(event);
        if (ImageItem *hit = m_view->targetItem()) {
            m_view->hostItemInteract().beginMove(hit, hit->placement());
        }
        emit m_view->statusChanged();
        return true;
}


// --- Workspace delete selection (Tier 6g) ---

bool WorkspaceController::tryKeyPressDeleteSelection(QKeyEvent *event)
{
    if (!m_view->isWorkspaceMode()) {
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
    // Workspace: hide from canvas only; session membership stays.
    // Destroy by item pointer (same path may exist twice after Duplicate).
    QList<ImageItem *> toRemove;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (m_view->liveItems().contains(item)) {
                toRemove.append(item);
            }
        }
    }
    if (toRemove.isEmpty()) {
        return false;
    }
    m_view->setUpdatesEnabled(false);
    scene->blockSignals(true);
    for (ImageItem *item : toRemove) {
        m_view->destroyCanvasItem(item);
    }
    scene->blockSignals(false);
    m_view->setUpdatesEnabled(true);
    if (m_view->viewport()) {
        m_view->viewport()->update();
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    event->accept();
    return true;
}


// --- Workspace shear keys (Tier 6h) ---

bool WorkspaceController::tryKeyPressShear(QKeyEvent *event)
{
    // Workspace: Alt+[ / Alt+] nudge horizontal shear; Alt+0 resets shear.
    if (!m_view->isWorkspaceMode()
        || !(event->modifiers() & Qt::AltModifier)
        || (event->modifiers() & Qt::ControlModifier)) {
        return false;
    }
    const int key = event->key();
    if (key != Qt::Key_BracketLeft && key != Qt::Key_BracketRight
        && key != Qt::Key_0) {
        return false;
    }
    const QList<QGraphicsItem *> selected =
        m_view->canvasScene() ? m_view->canvasScene()->selectedItems() : QList<QGraphicsItem *>();
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            if (m_view->liveItems().contains(item)) {
                targets.append(item);
            }
        }
    }
    if (targets.isEmpty()) {
        return false;
    }
    const qreal step = PlacementLinear::shearStepFromModifiers(
        event->modifiers() & Qt::ShiftModifier);
    for (ImageItem *item : targets) {
        ItemComponents::Placement pl = item->placement();
        if (key == Qt::Key_0) {
            pl.shear = 0.0;
        } else {
            pl.shear = PlacementLinear::shearAfterKey(
                pl.shear, step, key == Qt::Key_BracketRight);
        }
        GalleryLayout::applyItemPlacement(item, pl);
        m_view->commitItemSessionEdit(item);
    }
    emit m_view->statusChanged();
    event->accept();
    return true;
}

bool WorkspaceController::tryKeyPressSelectAll(QKeyEvent *event)
{
    // Gallery / Workspace: Ctrl+A selects every live tile (standard multi-select).
    if (!(m_view->isGalleryMode() || m_view->isWorkspaceMode())
        || event->key() != Qt::Key_A
        || !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))
        || (event->modifiers() & (Qt::ShiftModifier | Qt::AltModifier))) {
        return false;
    }
    selectAllCanvasItems();
    event->accept();
    return true;
}


// --- Workspace selection pack layout ---

bool WorkspaceController::layoutItems(const GalleryLayout::Params &userParams,
                                     const QList<ImageItem *> &itemsIn)
{
    if (!m_view->isWorkspaceMode() || !m_view->canvasScene()) {
        return false;
    }
    QList<ImageItem *> items = itemsIn;
    if (items.isEmpty()) {
        items = m_view->transformTargets();
    }
    // Require an explicit multi-item or single selection on the canvas —
    // do not fall back to “sole item” when nothing is selected in Workspace.
    if (items.isEmpty()) {
        for (QGraphicsItem *gi : m_view->canvasScene()->selectedItems()) {
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
        ? m_view->mapToScene(m_view->viewport()->rect().center())
        : beforeBounds.center();

    QVector<ItemComponents::Placement> befores;
    befores.reserve(items.size());
    for (ImageItem *item : items) {
        befores.append(item->placement());
    }

    GalleryLayout::Params params = userParams;
    const qreal margin = params.margin > 0 ? params.margin : 16.0;
    params.margin = margin;
    if (params.gap <= 0) {
        params.gap = 12.0;
    }
    params.availW = GalleryPackFit::packAvailAxis(m_view->viewport()->width(), margin);
    params.availH = GalleryPackFit::packAvailAxis(m_view->viewport()->height(), margin);

    // Workspace layout is axis-aligned placement; clear free-form tilt/flips
    // on the targets only (content bakes stay in pixels).
    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        ItemComponents::Placement pl = item->placement();
        pl.rotation = 0.0;
        pl.hFlip = false;
        pl.vFlip = false;
        GalleryLayout::applyItemPlacement(item, pl);
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
                    ItemComponents::Placement pl = item->placement();
                    pl.pos += delta;
                    GalleryLayout::applyItemPlacement(item, pl);
                }
            }
        }
    }

    if (m_view->hostUndoStack()) {
        m_view->hostUndoStack()->beginMacro(m_view->tr("Layout selection"));
        for (int i = 0; i < items.size(); ++i) {
            ImageItem *item = items.at(i);
            if (!item) {
                continue;
            }
            m_view->pushItemGeometryCommand(m_view->tr("Layout selection"), item, befores.at(i),
                                    item->placement());
        }
        m_view->hostUndoStack()->endMacro();
    }

    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        // Layout only moves pose — sparse Placement; do not re-stamp content tables.
        const ItemComponents::Placement pl = item->placement();
        if (item->sessionId() != kInvalidSessionImageId) {
            m_view->itemWorld().setPlacement(item->sessionId(), pl);
        } else if (!item->path().isEmpty()) {
            WorkspaceItemState s;
            if (const WorkspaceItemState *prev = m_view->itemWorld().getPathState(item->path())) {
                s = *prev;
            }
            ItemComponents::applyPlacementToState(s, pl);
            m_view->itemWorld().setPathState(item->path(), s);
        }
    }

    updateSceneRect();
    m_view->viewport()->update();
    emit m_view->statusChanged();
    return true;
}


void WorkspaceController::applyFreeFormLayout()
{
    if (!m_view->isWorkspaceMode()) {
        return;
    }
    if (!m_view->hostLayout().isFreeForm()) {
        // Should not happen in Workspace (always FreeForm).
    }
    m_view->hostLayout().setMode(LayoutMode::FreeForm);
    for (ImageItem *item : m_view->liveItems()) {
        m_view->applyItemModeFlags(item);
    }
    restoreFreeFormStates();
    if (!m_view->liveItems().isEmpty() && m_view->canvasScene()) {
        m_view->canvasScene()->setSceneRect(
            ViewTransform::padded(m_view->canvasScene()->itemsBoundingRect(),
                                  ViewTransform::kFreeformScenePad));
    }
    m_view->hostFraming().releaseFit();
    emit m_view->statusChanged();
}

void WorkspaceController::reloadFromDisk()
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
        m_view->hostDisplayPipeline().scheduleImageLoad(path, static_cast<int>(ImageView::LoadAdd));
    }
    m_view->hostHud().showFlash(ImageView::tr("Reload"), ImageView::tr("Workspace"), [v = m_view]() { if (v && v->viewport()) v->viewport()->update(); });
    emit m_view->statusChanged();
}

void WorkspaceController::hardReloadFromDisk()
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

    auto finish = [this, binds, tileTotal]() {
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
            m_view->hostDisplayPipeline().scheduleImageLoad(
                b.path, static_cast<int>(ImageView::LoadAdd));
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

void WorkspaceController::setTool(Tool tool)
{
    if (m_tool == tool) {
        return;
    }
    m_tool = tool;
    m_view->setCursor(ToolPolicy::cursorFor(m_tool));
    if (m_view->isWorkspaceMode()) {
        applyToolDragMode();
    }
}

void WorkspaceController::applyToolDragMode()
{
    m_view->setDragMode(ToolPolicy::workspaceRubberBand(m_tool)
                            ? QGraphicsView::RubberBandDrag
                            : QGraphicsView::NoDrag);
}

int WorkspaceController::uniqueWeakPathCount() const
{
    if (!m_view) {
        return 0;
    }
    QSet<QString> weakPaths;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || item->path().isEmpty()) {
            continue;
        }
        const int edge = item->displayPixelLongEdge();
        if (!item->hasDisplayPixels()
            || edge <= DisplayQuality::kLqipMaxEdge) {
            weakPaths.insert(item->path());
        }
    }
    return weakPaths.size();
}
