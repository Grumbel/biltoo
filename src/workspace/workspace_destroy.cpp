// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Live/stash tile teardown. ImageView keeps a thin public host router.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "display/displaypipelinecontroller.h"
#include "gallery/gallerycontroller.h"
#include "display/tileneighborprefetch.h"
#include "workspace/grouptransformsession.h"
#include "item/iteminteractsession.h"

#include <QGraphicsScene>
#include <QUndoStack>

void WorkspaceController::destroyCanvasItem(ImageItem *item, bool persistState)
{
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Stage 2: drop tile session and release pipeline-owned bag before delete.
    m_view->hostDisplayPipeline().dropItemTileLodSession(item);
    m_view->hostDisplayPipeline().releaseTileBag(item);
    m_view->hostDisplayPipeline().unregisterItemDisplaySurface(item);
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_view->liveItems().contains(item);
    const bool inGalleryStash = m_view->hostGallery().stashedItems().contains(item);
    const bool inWorkspaceStash = stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    itemInteract().dropIfItem(item);
    if (item == m_view->hostGallery().selectionAnchor()) {
        m_view->hostGallery().setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (groupSession().isScaleDrag() || groupSession().isRotateDrag()
        || groupSession().hasDragItems()) {
        groupSession().endDrag();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_view->hostGallery().stashedItems().removeAll(item);
    stashedItems().removeAll(item);

    // Default: snapshot bound appearance/pose before the tile is gone.
    // Session-id *delete* paths pass persistState=false after removeAppearance
    // so this cannot re-insert the DTO that was just cleared.
    if (persistState) {
        m_view->hostRememberItemState(item);
    }
    m_view->liveItems().removeAll(item);
    // Off-canvas neighbor prefetch may still hold a controller for this path.
    if (!path.isEmpty() && !pathOnLiveCanvas(path)) {
        m_view->hostTileNeighborPrefetch().dropPath(path);
    }
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        // Hide before remove so any stray paint of this item is a no-op.
        item->setVisible(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
        item->setVisible(false);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        if (!m_view->hostPreserveUndoOnDestroy()) {
            stack->clear();
        }
    }
    // Batch delete (Delete key) keeps updates disabled and rebuilds the scene
    // index once at the end — avoid BSP walk / setSceneRect mid-loop (SIGSEGV
    // in QGraphicsSceneBspTree::climbTree during processDirtyItems).
    if (m_view->isWorkspaceMode() && m_view->updatesEnabled()) {
        updateSceneRect();
    }
}
