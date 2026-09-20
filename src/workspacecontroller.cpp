// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacecontroller.h"
#include "itemcomponents.h"
#include <memory>
#include <QSet>
#include <QFileInfo>
#include "thumtoocache.h"
#include "imagecache.h"
#include "viewtransform.h"
#include "imageview.h"
#include "sessionbindbook.h"
#include <QUndoStack>
#include "gallerypackfit.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "placementlinear.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGraphicsScene>
#include "imageitem.h"
#include "placementlinear.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QScrollBar>
#include <QtMath>

WorkspaceController::WorkspaceController(ImageView *view)
    : m_view(view)
{
}

void WorkspaceController::snapshot()
{
    m_savedItems.clear();
    for (ImageItem *item : m_view->liveItems()) {
        const WorkspaceItemState s = m_view->captureState(item);
        m_savedItems.append(s);
        // Path map is session/Image appearance only; unbound duplicates stay
        // in the list and must not collapse into a single path entry.
        if (s.sessionId != kInvalidSessionImageId) {
            m_view->itemWorld().setAppearance(s.sessionId, s);
        }
        if (s.sessionIndex >= 0) {
            m_view->setItemStateForPath(s.path, s);
        }
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
    m_view->clearPendingWorkspacePaths();
    // Merge session appearance (crop / flip / orientation) from the live map into
    // the durable snapshot so Image-mode edits survive a full rebuild.
    for (WorkspaceItemState &slot : m_savedItems) {
        if (slot.sessionId != kInvalidSessionImageId) {
            if (const WorkspaceItemState *sit = m_view->itemWorld().getAppearance(slot.sessionId)) {
                slot.hasCrop = sit->hasCrop;
                slot.cropRect = sit->cropRect;
                slot.hFlip = sit->hFlip;
                slot.vFlip = sit->vFlip;
                slot.contentQuarterTurns = sit->contentQuarterTurns;
                slot.contentHFlip = sit->contentHFlip;
                slot.contentVFlip = sit->contentVFlip;
                continue;
            }
        }
        const WorkspaceItemState *it = m_view->itemStateForPath(slot.path);
        if (!it) {
            continue;
        }
        // Path-level appearance only for matching bound session slots (legacy).
        if (slot.sessionIndex < 0 || it->sessionIndex < 0
            || slot.sessionIndex != it->sessionIndex) {
            continue;
        }
        slot.hasCrop = it->hasCrop;
        slot.cropRect = it->cropRect;
        slot.hFlip = it->hFlip;
        slot.vFlip = it->vFlip;
        slot.contentQuarterTurns = it->contentQuarterTurns;
        slot.contentHFlip = it->contentHFlip;
        slot.contentVFlip = it->contentVFlip;
        slot.orientation = 0.0;
    }
    // AUDIT M27: queue every saved state (including duplicate paths) then load.
    m_view->setPendingRestoreStates(m_savedItems);
    for (const WorkspaceItemState &state : m_savedItems) {
        m_view->setItemStateForPath(state.path, state);
        m_view->scheduleRestoreLoad(state.path);
    }
    m_view->clearFitFillModes();
    // Apply zoom before scene-rect expansion; pan after range is valid.
    if (m_hasSavedView) {
        m_view->setTransform(m_savedViewTransform);
    }
    m_view->updateWorkspaceSceneRect();
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
    m_freeFormStates.clear();
    m_hasFreeFormViewTransform = false;
}

void WorkspaceController::stashItems()
{
    // Replace any previous workspace stash (e.g. nested mode switches).
    discardStash();
    // Zoom lives in the view matrix; pan lives in scrollbars — capture both.
    // Scene centre is robust across sceneRect rebuilds (same idea as Gallery).
    m_stashedViewTransform = m_view->transform();
    m_stashedViewCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    m_hasStashedView = true;
    if (m_view->liveItems().isEmpty()) {
        return;
    }
    m_stashedItems = m_view->liveItems();
    m_view->clearInteractionState();
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
    while (!m_view->liveItems().isEmpty()) {
        m_view->destroyCanvasItem(m_view->liveItems().last());
    }
    if (m_view->canvasScene()) {
        m_view->canvasScene()->blockSignals(true);
        m_view->canvasScene()->clear();
        m_view->canvasScene()->blockSignals(false);
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
        const WorkspaceItemState *app = nullptr;
        WorkspaceItemState pathFallback;
        if (item->sessionId() != kInvalidSessionImageId) {
            if (const WorkspaceItemState *sit = m_view->itemWorld().getAppearance(item->sessionId())) {
                app = sit;
            }
        }
        if (!app) {
            int samePath = 0;
            for (ImageItem *peer : m_view->liveItems()) {
                if (peer && peer->path() == item->path()) {
                    ++samePath;
                }
            }
            if (samePath == 1) {
                if (const WorkspaceItemState *st = m_view->itemStateForPath(item->path())) {
                    pathFallback = *st;
                    app = &pathFallback;
                }
            }
        }
        if (!app) {
            continue;
        }
        // content ops require full on-disk host for crop / content
        // flips / quarter-turns. Stashed tiles often already hold baked crop
        // pixels (peer sync while in Image mode). Reloading only on size
        // mismatch alternated wrong/correct every Workspace↔Image cycle:
        // match → skip reload → double-crop → mismatch → reload → OK → …
        const bool needsFullSource = app->hasCrop || app->contentQuarterTurns != 0
            || app->contentHFlip || app->contentVFlip
            || item->sourceImage().isNull();
        if (needsFullSource) {
            const QImage full = m_view->fullRasterForEdit(item->path());
            if (!full.isNull()) {
                // Raw on-disk reload → single gate (avoids double-bake on cycle).
                m_view->hostDisplayPipeline().installDisplayPixels(
                    item, full, SessionAppearance::PixelKind::FullSource,
                    item->sessionId());
            } else {
                // No full host: rematerialize from ImageCache soft/host (stand-in
                // + async). Do not chrome-only on multi-MP — cannot
                // bake crop on the GUI and must not claim applied == want.
                m_view->rematerializeItemContent(item, *app);
            }
        } else {
            // Grade-only (or content already on soft): rematerialize so multi-MP
            // still gets grade via soft stand-in + async. A chrome-only path
            // alone leaves multi-MP grade as chrome-only without a bake.
            m_view->rematerializeItemContent(item, *app);
        }
    }
    m_view->clearFitFillModes();
    // Order: zoom → expand sceneRect for the new scale → pan to saved centre.
    // updateWorkspaceSceneRect alone would leave scrollbars at Image-mode zeros.
    if (m_hasStashedView) {
        m_view->setTransform(m_stashedViewTransform);
    }
    m_view->updateWorkspaceSceneRect();
    if (m_hasStashedView) {
        m_view->centerOn(m_stashedViewCenter);
        m_hasStashedView = false;
    }
    m_view->viewport()->update();
}

void WorkspaceController::snapshotFreeFormStates()
{
    m_freeFormStates.clear();
    for (ImageItem *item : m_view->liveItems()) {
        m_freeFormStates.insert(item->path(), m_view->captureState(item));
    }
    m_freeFormViewTransform = m_view->transform();
    m_hasFreeFormViewTransform = true;
}

void WorkspaceController::restoreFreeFormStates()
{
    for (ImageItem *item : m_view->liveItems()) {
        const auto it = m_freeFormStates.constFind(item->path());
        if (it != m_freeFormStates.constEnd()) {
            m_view->applyState(item, *it);
            m_view->setItemStateForPath(item->path(), *it);
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
    // later discarded (e.g. entering Gallery).
    snapshot();
    if (next == ImageView::ViewMode::Image) {
        // Keep free-form tiles + pixels + view for a fast return to Workspace.
        stashItems();
    }
    // Gallery path: GalleryController::enter discards the workspace stash after
    // packing from live items / session paths.
}

void WorkspaceController::enter(int previousMode)
{
    const auto previous = static_cast<ImageView::ViewMode>(previousMode);
    m_view->setActiveMode(ImageView::ViewMode::Workspace, LayoutMode::FreeForm);
    m_view->applyToolDragMode();
    bool keepViewTransform = false;
    if (previous == ImageView::ViewMode::Image && !m_stashedItems.isEmpty()) {
        // Fast path: reattach live items (no re-decode).
        restoreStashedItems();
        keepViewTransform = true; // stashed view includes user zoom
    } else if (!m_savedItems.isEmpty()) {
        // Durable snapshot — permanent Workspace across Gallery↔Workspace and
        // when the Image-mode stash was discarded. Never adopt Gallery packing
        // as the free-form canvas (that silently overwrote user arrangement).
        const bool hadSavedView = m_hasSavedView;
        restore();
        keepViewTransform = hadSavedView;
    } else {
        // Empty permanent Workspace. Never adopt whatever Image/Gallery is
        // currently showing — only filmstrip drop / project load / explicit place
        // load places tiles. (Previously only Gallery was cleared; Image→
        // Workspace left the classic single tile on the free-form canvas.)
        m_view->clearLiveCanvas();
        // Drop gallery pathOrder / in-flight LoadAdd so background Gallery
        // decodes cannot recreate session tiles on this blank canvas.
        m_view->invalidateGalleryDecodes();
        m_view->clearPathOrder();
        m_view->applyModeFlagsToLiveItems();
    }
    // Always clear canvas selection on enter — restored stash may keep old
    // selected flags, which MainWindow would mirror onto every filmstrip row.
    if (m_view->canvasScene()) {
        m_view->canvasScene()->clearSelection();
    }
    // Fresh Workspace (no stashed/saved camera): overview scale, not 1:1.
    if (!keepViewTransform) {
        m_view->setWorkspaceDefaultViewScale();
    }
    m_view->updateWorkspaceSceneRect();
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
        if (!itemHit && m_view->pageGuideVisible()
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
            m_view->hostItemInteract().beginMove(hit, m_view->captureState(hit));
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
        item->applyPlacement(pl);
        m_view->commitItemSessionEdit(item);
    }
    emit m_view->statusChanged();
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

    QVector<WorkspaceItemState> befores;
    befores.reserve(items.size());
    for (ImageItem *item : items) {
        befores.append(m_view->captureState(item));
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
        item->applyPlacement(pl);
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
                    item->applyPlacement(pl);
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
                                    m_view->captureState(item));
        }
        m_view->hostUndoStack()->endMacro();
    }

    for (ImageItem *item : items) {
        if (!item) {
            continue;
        }
        const WorkspaceItemState s = m_view->captureState(item);
        if (item->sessionId() != kInvalidSessionImageId) {
            // Layout only moves pose; setPlacement is the Stage 2 write path
            // (dual-writes DTO pose fields). Full setAppearance keeps content
            // fields from capture in sync for undo/filmstrip.
            m_view->itemWorld().setPlacement(
                item->sessionId(), ItemComponents::placementFromState(s));
            m_view->itemWorld().setAppearance(item->sessionId(), s);
        }
        m_view->itemWorld().setPathState(item->path(), s);
    }

    m_view->updateWorkspaceSceneRect();
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
        m_view->hostDisplayPipeline().scheduleImageLoad(path, static_cast<int>(ImageView::LoadAdd));
    }
    m_view->flashHud(ImageView::tr("Reload"), ImageView::tr("Workspace"));
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
