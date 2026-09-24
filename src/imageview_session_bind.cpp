// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds + thin routers for LoadAdd placement (Workspace owns body).

#include "imageview.h"
#include "imageitem.h"
#include "session/sessionbindbook.h"

void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    m_workspace.purgeSatisfiedPendingBinds(path);
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
    return m_workspace.takePendingSessionBindForNewItem(path, item, out);
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
