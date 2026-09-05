// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
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
            m_view->appearance().set(s.sessionId, s);
        }
        if (s.sessionIndex >= 0) {
            m_view->itemStates().insert(s.path, s);
        }
    }
    // Durable view backup when the live stash is later discarded (e.g. Gallery).
    m_savedViewTransform = m_view->transform();
    m_savedViewCenter = m_view->mapToScene(m_view->viewport()->rect().center());
    m_hasSavedView = true;
}

void WorkspaceController::restore()
{
    m_view->clearWorkspace();
    m_view->clearPendingWorkspacePaths();
    // Merge session appearance (crop / flip / orientation) from the live map into
    // the durable snapshot so Image-mode edits survive a full rebuild.
    for (WorkspaceItemState &slot : m_savedItems) {
        if (slot.sessionId != kInvalidSessionImageId) {
            if (const WorkspaceItemState *sit = m_view->appearance().get(slot.sessionId)) {
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
        const auto it = m_view->itemStates().constFind(slot.path);
        if (it == m_view->itemStates().cend()) {
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
    m_view->pendingRestoreStates() = m_savedItems;
    for (const WorkspaceItemState &state : m_savedItems) {
        m_view->itemStates().insert(state.path, state);
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
    for (ImageItem *item : m_stashedItems) {
        if (!item) {
            continue;
        }
        if (QGraphicsScene *sc = item->scene()) {
            sc->removeItem(item);
        }
        delete item;
    }
    m_stashedItems.clear();
    m_hasStashedView = false;
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
            if (const WorkspaceItemState *sit = m_view->appearance().get(item->sessionId())) {
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
                const auto it = m_view->itemStates().constFind(item->path());
                if (it != m_view->itemStates().cend()) {
                    pathFallback = *it;
                    app = &pathFallback;
                }
            }
        }
        if (!app) {
            continue;
        }
        // applyContentToItem requires full on-disk pixels for crop / content
        // flips / quarter-turns. Stashed tiles often already hold baked crop
        // pixels (peer sync while in Image mode). Reloading only on size
        // mismatch alternated wrong/correct every Workspace↔Image cycle:
        // match → skip reload → double-crop → mismatch → reload → OK → …
        const bool needsFullSource = app->hasCrop || app->contentQuarterTurns != 0
            || app->contentHFlip || app->contentVFlip
            || item->sourceImage().isNull();
        if (needsFullSource) {
            const QImage full = ImageLoader::load(item->path());
            if (!full.isNull()) {
                item->setSourceImage(full);
            }
        }
        SessionAppearance::applyContentToItem(item, *app);
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
            m_view->itemStates().insert(item->path(), *it);
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
    m_view->setActiveMode(ImageView::ViewMode::Workspace, ImageView::LayoutMode::FreeForm);
    m_view->applyToolDragMode();
    if (previous == ImageView::ViewMode::Image && !m_stashedItems.isEmpty()) {
        // Fast path: reattach live items (no re-decode).
        restoreStashedItems();
    } else if (!m_savedItems.isEmpty()) {
        // Durable snapshot — permanent Workspace across Gallery↔Workspace and
        // when the Image-mode stash was discarded. Never adopt Gallery packing
        // as the free-form canvas (that silently overwrote user arrangement).
        restore();
    } else {
        // Empty permanent Workspace. Never adopt whatever Image/Gallery is
        // currently showing — only filmstrip drop / double-click / project
        // load places tiles. (Previously only Gallery was cleared; Image→
        // Workspace left the classic single tile on the free-form canvas.)
        m_view->clearLiveCanvas();
        // Drop gallery pathOrder / in-flight LoadAdd so background Gallery
        // decodes cannot recreate session tiles on this blank canvas.
        m_view->invalidateGalleryDecodes();
        m_view->pathOrder().clear();
        m_view->sessionIdOrder().clear();
        m_view->applyModeFlagsToLiveItems();
    }
    // Always clear canvas selection on enter — restored stash may keep old
    // selected flags, which MainWindow would mirror onto every filmstrip row.
    if (m_view->canvasScene()) {
        m_view->canvasScene()->clearSelection();
    }
    m_view->updateWorkspaceSceneRect();
    emit m_view->statusChanged();
}

