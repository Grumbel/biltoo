// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Group scale/rotate and single-item handle/move release live on
// WorkspaceController (workspace_group.cpp). ImageView is input routing only.

#include "imageview.h"

#include <QMouseEvent>

bool ImageView::tryMouseMoveGroupAndHandleDrag(QMouseEvent *event)
{
    return m_workspace.tryMouseMoveGroupAndHandleDrag(event);
}

bool ImageView::tryMouseReleaseGroupDrag(QMouseEvent *event)
{
    return m_workspace.tryMouseReleaseGroupDrag(event);
}

bool ImageView::tryMouseReleaseHandleDrag(QMouseEvent *event)
{
    return m_workspace.tryMouseReleaseHandleDrag(event);
}
