// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include "imageview_types.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTransform>

class ImageView;
class ImageItem;

/**
 * Workspace-mode collaborator for ImageView.
 *
 * Owns free-form placement snapshot state, the durable Workspace snapshot,
 * and the live tile stash used while Image mode is active.
 * ImageView remains the QGraphicsView shell and public API surface.
 */
class WorkspaceController
{
public:
    explicit WorkspaceController(ImageView *view);

    void snapshot();
    void restore();
    void discardStash();
    void stashItems();
    void restoreStashedItems();

    /** Enter Workspace mode from @p previous (setViewMode Workspace branch). */
    /** Enter Workspace mode from previous ViewMode (int = ImageView::ViewMode). */
    void enter(int previousMode);
    void snapshotFreeFormStates();
    void restoreFreeFormStates();

    bool hasStash() const { return !m_stashedItems.isEmpty(); }
    QList<ImageItem *> &stashedItems() { return m_stashedItems; }
    const QList<ImageItem *> &stashedItems() const { return m_stashedItems; }

    QList<WorkspaceItemState> &savedItems() { return m_savedItems; }
    const QList<WorkspaceItemState> &savedItems() const { return m_savedItems; }
    bool hasSavedView() const { return m_hasSavedView; }
    QTransform savedViewTransform() const { return m_savedViewTransform; }
    QPointF savedViewCenter() const { return m_savedViewCenter; }

    bool hasStashedView() const { return m_hasStashedView; }
    QTransform stashedViewTransform() const { return m_stashedViewTransform; }
    QPointF stashedViewCenter() const { return m_stashedViewCenter; }

    QHash<QString, WorkspaceItemState> &freeFormStates() { return m_freeFormStates; }
    const QHash<QString, WorkspaceItemState> &freeFormStates() const { return m_freeFormStates; }
    bool hasFreeFormViewTransform() const { return m_hasFreeFormViewTransform; }
    QTransform freeFormViewTransform() const { return m_freeFormViewTransform; }

private:
    ImageView *m_view = nullptr;

    QList<WorkspaceItemState> m_savedItems;
    QList<ImageItem *> m_stashedItems;
    QTransform m_stashedViewTransform;
    QPointF m_stashedViewCenter;
    bool m_hasStashedView = false;
    QTransform m_savedViewTransform;
    QPointF m_savedViewCenter;
    bool m_hasSavedView = false;

    QHash<QString, WorkspaceItemState> m_freeFormStates;
    QTransform m_freeFormViewTransform;
    bool m_hasFreeFormViewTransform = false;
};

#endif // WORKSPACECONTROLLER_H
