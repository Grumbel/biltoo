// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds + thin routers for LoadAdd placement (Workspace owns body).

#include "imageview.h"
#include "imageitem.h"
#include "session/sessionbindbook.h"

void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    for (int bi = m_session.bindBook().bindCount() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_session.bindBook().bindAt(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_session.bindBook().removeBindAt(bi);
        }
    }
}

void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    m_workspace.applyPendingBindScenePos(item, bound);
}

bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    return m_workspace.installFullPreservingWorkspaceFootprint(item, image);
}

bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    if (!out || path.isEmpty() || !item) {
        return false;
    }
    for (int bi = 0; bi < m_session.bindBook().bindCount(); ++bi) {
        if (m_session.bindBook().bindAt(bi).path != path) {
            continue;
        }
        const PendingSessionBind candidate = m_session.bindBook().bindAt(bi);
        if (candidate.id != kInvalidSessionImageId) {
            if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                if (owner != item) {
                    m_session.bindBook().removeBindAt(bi);
                    --bi;
                    continue;
                }
            }
        }
        m_session.bindBook().takeBindAt(bi, out);
        if (out->id != kInvalidSessionImageId) {
            // List-order refresh + applied→ItemWorld migrate (2083/2084).
            setItemSessionId(item, out->id);
            if (sessionListIndex(item) < 0 && out->index >= 0) {
                item->setSessionIndex(out->index);
            }
        } else if (out->index >= 0) {
            item->setSessionIndex(out->index);
        }
        return true;
    }
    return false;
}

void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                                    bool haveBound, const PendingSessionBind &bound)
{
    m_workspace.placeNewLoadAddItem(item, path, image, haveBound, bound);
}

QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    return m_workspace.collectItemsForSessionId(sessionId);
}

QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    return m_workspace.destroySessionIdItems(doomed);
}
