// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"

void ImageView::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    m_workspace.prunePendingBindsAndSavedForSessionId(sessionId);
}

void ImageView::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    m_workspace.prunePathOrdersAfterSessionRemove(removedPaths);
}

void ImageView::restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                                  const QPointF &keptCenter, int scrollH, int scrollV)
{
    m_workspace.restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH,
                                                  scrollV);
}

void ImageView::removeWorkspaceSessionId(SessionImageId sessionId)
{
    m_workspace.removeWorkspaceSessionId(sessionId);
}

void ImageView::setCurrentSessionId(SessionImageId id)
{
    if (m_session.identity().currentId == id) {
        return;
    }
    m_session.identity().setCurrentId(id);
    // Attention marker is per SessionImageId — reload draft for the new image.
    m_attentionCtrl.onCurrentSessionChanged();
}

void ImageView::removeCanvasSessionIds(const QList<SessionImageId> &ids)
{
    m_workspace.removeCanvasSessionIds(ids);
}

void ImageView::placeSessionIdsOnCanvas(const QList<SessionImageId> &ids,
                                        const QStringList &paths,
                                        const QList<int> &sessionIndices)
{
    m_workspace.placeSessionIdsOnCanvas(ids, paths, sessionIndices);
}
