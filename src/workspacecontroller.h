// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include "imageview_types.h"
#include "itemcomponents.h"
#include "gallerylayout.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QString>
#include <QTransform>

class ImageView;
class ImageItem;
class QMouseEvent;
class QKeyEvent;

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
    /** Drop durable free-form arrangement (session open is not a project). */
    void clearDurableSnapshot();
    void stashItems();
    void restoreStashedItems();

    /** Enter Workspace mode from @p previous (setViewMode Workspace branch). */
    /** Enter Workspace mode from previous ViewMode (int = ImageView::ViewMode). */
    void enter(int previousMode);
    /** Leaving Workspace: durable snapshot; stash live tiles when next is Image. */
    void onLeave(int nextMode);
    void snapshotFreeFormStates();
    void restoreFreeFormStates();

    QList<ImageItem *> &stashedItems() { return m_stashedItems; }
    const QList<ImageItem *> &stashedItems() const { return m_stashedItems; }

    QList<WorkspaceItemState> &savedItems() { return m_savedItems; }
    const QList<WorkspaceItemState> &savedItems() const { return m_savedItems; }

    /** Select tool: item hit / page-guide / rubber-band (Tier 6d). */
    bool tryMousePressSelect(QMouseEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);
    bool tryKeyPressShear(QKeyEvent *event);
    bool layoutItems(const GalleryLayout::Params &userParams,
                     const QList<ImageItem *> &itemsIn = {});
    /** ImageView::setLayoutMode(FreeForm) body — Workspace only. */
    void applyFreeFormLayout();
    void reloadFromDisk();
    void hardReloadFromDisk();

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

    /** Free-form pose while Gallery layout temporarily packs tiles. */
    QHash<SessionImageId, ItemComponents::Placement> m_freeFormById;
    QHash<QString, ItemComponents::Placement> m_freeFormByPath;
    QTransform m_freeFormViewTransform;
    bool m_hasFreeFormViewTransform = false;
};

#endif // WORKSPACECONTROLLER_H
