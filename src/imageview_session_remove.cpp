// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"
#include "session/packorderview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"
#include "display/imagecache.h"
#include "session/sessionappearance.h"
#include "session/sessionbindbook.h"
#include "view/viewtransform.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QScrollBar>

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
    m_workspace.restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH, scrollV);
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
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.session().clearDraft();
        m_attentionCtrl.ensureAttentionPoint();
        if (viewport()) {
            viewport()->update();
        }
    }
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

