// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Workspace clipboard capture / paste placement. ImageView keeps thin routers.
// Duplicate (Workspace + Gallery) stays on ImageView until a shared host is clear.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QWidget>

QList<WorkspaceItemState> WorkspaceController::captureSelectedClipboard() const
{
    QList<WorkspaceItemState> out;
    if (!m_view->isWorkspaceMode()) {
        return out;
    }
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return out;
    }
    for (QGraphicsItem *gi : scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || !m_view->liveItems().contains(item)) {
            continue;
        }
        // Stage 4a / 2: freeze policy (store + live when durable, not mid-edit).
        const SessionImageId sid = item->sessionId();
        WorkspaceItemState s = m_view->freezeItemAppearance(item);
        s.path = item->path();
        s.sessionId = sid;
        out.append(s);
    }
    return out;
}

void WorkspaceController::placeClipboardItems(const QList<WorkspaceItemState> &items,
                                              const QVector<SessionImageId> &newIds,
                                              const QList<int> &sessionIndices)
{
    if (!m_view->isWorkspaceMode() || items.isEmpty() || newIds.size() != items.size()) {
        return;
    }
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->clearSelection();
    }
    m_view->hostBindBook().clearSelectIds();
    for (int i = 0; i < items.size(); ++i) {
        const WorkspaceItemState &st = items.at(i);
        const SessionImageId sid = newIds.at(i);
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        if (st.path.isEmpty() || sid == kInvalidSessionImageId) {
            continue;
        }
        m_view->hostBindBook().addSelectId(sid);
        // Appearance (content + pose) must already be in the store under sid.
        m_view->addImageForSession(st.path, sid, idx);
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}
