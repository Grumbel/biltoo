// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Add / place / move images onto the canvas — thin routers to WorkspaceController.

#include "imageview.h"

bool ImageView::addImageForSession(const QString &path, SessionImageId sessionId,
                                     int sessionIndex)
{
    return m_workspace.addImageForSession(path, sessionId, sessionIndex);
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                                     SessionImageId sessionId, int sessionIndex)
{
    return m_workspace.placeOrMoveImageAt(path, scenePos, sessionId, sessionIndex);
}
