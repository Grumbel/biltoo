// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Live-canvas and interaction teardown. ImageView keeps thin routers.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "gallery/gallerycontroller.h"
#include "item/iteminteractsession.h"
#include "util/biltoo_logging.h"

#include <QSet>
#include <QUndoStack>

void WorkspaceController::clearInteractionState()
{
    itemInteract().clear();
    clearGroupTransform();
    m_view->hostGallery().clearChrome();
}

void WorkspaceController::clearLiveCanvas()
{
    // Destroy only the live scene items. Mode stashes (Workspace/Gallery tiles
    // kept while in Image mode) must survive Image-mode LoadReplace / Next.
    clearInteractionState();
    if (QUndoStack *stack = m_view->hostUndoStack()) {
        stack->clear();
    }
    QSet<ImageItem *> protectedStash;
    for (ImageItem *item : stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    for (ImageItem *item : m_view->hostGallery().stashedItems()) {
        if (item) {
            protectedStash.insert(item);
        }
    }
    // Snapshot unique pointers — live list must never hold duplicates, but if it
    // does, destroying by index while mutating the list is unsafe.
    QList<ImageItem *> doomed;
    QSet<ImageItem *> seen;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && !seen.contains(item)) {
            seen.insert(item);
            // Structural: never free a pointer that mode-stash still owns.
            if (protectedStash.contains(item)) {
                biltooModeDbg("clearLiveCanvas SKIP stashed ptr path=%s",
                              qPrintable(item->path()));
                Q_ASSERT_X(false, "clearLiveCanvas",
                           "live list holds a mode-stashed ImageItem* — ownership bug");
                continue;
            }
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        destroyCanvasItem(item);
    }
    m_view->liveItems().clear();
    // Do not scene->clear() — that would delete stashed items if any were
    // still parented (they are not). Scene may hold no items; that is fine.
    m_view->hostChrome().clearMouseInfo();
    emit m_view->mouseInfoChanged(m_view->hostChrome().currentMouseInfo());
}
