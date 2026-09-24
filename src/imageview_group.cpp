// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Group scale/rotate *session* and chrome live on WorkspaceController
// (workspace_group.cpp). ImageView keeps input routing only.

#include "imageview.h"
#include "imageitem.h"
#include "item/itemcomponents.h"

#include <QMouseEvent>
#include <QUndoStack>

bool ImageView::tryMouseMoveGroupAndHandleDrag(QMouseEvent *event)
{
    if (m_workspace.groupSession().isScaleDrag()) {
        m_workspace.updateGroupScale(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_workspace.groupSession().isRotateDrag()) {
        m_workspace.updateGroupRotate(mapToScene(event->pos()), event->modifiers());
        viewport()->update();
        event->accept();
        return true;
    }
    if (m_workspace.itemInteract().isHandleDragging()) {
        m_workspace.itemInteract().currentHandleDragItem()->updateHandleInteraction(
            mapToScene(event->pos()), event->modifiers(), m_workspace.itemInteract().handlePressRef());
        viewport()->update();
        event->accept();
        return true;
    }
    return false;
}

bool ImageView::tryMouseReleaseGroupDrag(QMouseEvent *event)
{
    if (!(m_workspace.groupSession().isScaleDrag() || m_workspace.groupSession().isRotateDrag())
        || event->button() != Qt::LeftButton) {
        return false;
    }
    if (m_undoStack && m_workspace.groupSession().hasDragItems()) {
        m_undoStack->beginMacro(m_workspace.groupSession().isRotateDrag()
                                    ? tr("Rotate selection")
                                    : tr("Scale selection"));
        for (int i = 0; i < m_workspace.groupSession().dragCount(); ++i) {
            ImageItem *item = m_workspace.groupSession().dragItemAt(i);
            if (!item) {
                continue;
            }
            pushItemTransformUndo(item, m_workspace.groupSession().dragStartPlacementAt(i),
                                  placementFromItem(item), tr("Transform"));
        }
        m_undoStack->endMacro();
    }
    m_workspace.endGroupScale();
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}

bool ImageView::tryMouseReleaseHandleDrag(QMouseEvent *event)
{
    if (!m_workspace.itemInteract().isHandleDragging() || event->button() != Qt::LeftButton) {
        return false;
    }
    ImageItem *handleItem = m_workspace.itemInteract().currentHandleDragItem();
    handleItem->endHandleInteraction(m_workspace.itemInteract().handlePressRef().handle);
    pushItemTransformUndo(handleItem, m_workspace.itemInteract().currentDragStartPlacement(),
                          placementFromItem(handleItem), tr("Transform"));
    m_workspace.itemInteract().endHandleDrag();
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    event->accept();
    return true;
}
