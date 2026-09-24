// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef WORKSPACECONTROLLER_H
#define WORKSPACECONTROLLER_H

#include "imageview_types.h"
#include "item/itemcomponents.h"
#include "gallery/gallerylayout.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <QVector>
#include "workspace/grouptransformsession.h"
#include "item/iteminteractsession.h"
#include "workspace/pageguidesession.h"

class ImageView;
class ImageItem;
class QMouseEvent;
class QKeyEvent;
class QPainter;
class QPrinter;

/**
 * Workspace-mode collaborator for ImageView.
 *
 * Owns free-form placement snapshot state, the durable Workspace snapshot,
 * the live tile stash used while Image mode is active, multi-select
 * group scale/rotate (GroupTransformSession + chrome), print page-guide
 * (PageGuideSession), stack/opacity/placement resets, and clipboard place.
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



    ItemInteractSession &itemInteract() { return m_itemInteract; }
    const ItemInteractSession &itemInteract() const { return m_itemInteract; }

    /** Workspace / shell tool (Select / Pan / Zoom). */
    Tool currentTool() const { return m_tool; }
    void setTool(Tool tool);
    /** Sync QGraphicsView drag mode with current tool (Workspace Select rubber-band). */
    void applyToolDragMode();

    // Stack z-order (scene-overlap raise/lower) + opacity (Workspace-only).
    void raiseItem(ImageItem *item);
    void lowerItem(ImageItem *item);
    void raiseSelected();
    void lowerSelected();
    void opacityUp();
    void opacityDown();
    void opacityReset();
    void resetItemScale();
    void resetItemRotation();
    void resetItemShear();

    /** Clipboard capture (Workspace selection freeze) / paste place. */
    QList<WorkspaceItemState> captureSelectedClipboard() const;
    void placeClipboardItems(const QList<WorkspaceItemState> &items,
                             const QVector<SessionImageId> &newIds,
                             const QList<int> &sessionIndices);

    /** Scene rect halo, empty placement, and session content presence. */
    bool hasContent() const;
    void updateSceneRect();
    QPointF findEmptyPlacement(const QSizeF &itemSize) const;

    /** Path membership: tiles to drop on setWorkspacePaths + default pose. */
    QList<ImageItem *> collectDoomedItems(const QStringList &paths,
                                          const QVector<SessionImageId> &sessionIds) const;
    void destroyDoomedItems(const QList<ImageItem *> &doomed);
    WorkspaceItemState defaultStateForPath(const QString &path, int ordinal) const;
    /** Reorder live tiles to match session/pack order (id-prefer, path fallback). */
    void reorderItemsByPaths(const QStringList &paths,
                             const QVector<SessionImageId> &ids = {});
    /** Refresh live tile SessionImageId / list-index from session document. */
    void rebindSession(const QStringList &sessionFiles,
                       const QVector<SessionImageId> &sessionIds);
    /** Post path membership: reorder, rebind, gallery pack/decode pulse. */
    void finishPathsSet(bool haveIds, const QStringList &paths,
                        const QVector<SessionImageId> &sessionIds);
    /** Session membership ensure + load schedule (Gallery/Workspace). */
    void setPaths(const QStringList &paths,
                  const QVector<SessionImageId> &sessionIds);
    int pathOccurrenceCount(const QString &path) const;
    bool pathOnLiveCanvas(const QString &path) const;
    void selectAllCanvasItems();

    // Canvas selection queries / select-by-id (MainWindow + transform targets).
    void selectBySessionIndices(const QList<int> &indices);
    void selectBySessionIds(const QList<SessionImageId> &ids);
    void selectPathsByOccurrence(const QStringList &paths);
    QList<SessionImageId> selectedSessionIds() const;
    QList<int> selectedSessionIndices() const;
    QList<ImageItem *> transformTargets() const;
    bool hasTransformTargets() const;
    bool hasSingleCropTarget() const;
    ImageItem *primaryItem() const;
    ImageItem *targetItem() const;
    /** Teardown live/stash tile (pipeline bags, scene, undo). */
    void destroyCanvasItem(ImageItem *item, bool persistState = true);
    void clearInteractionState();
    void clearLiveCanvas();
    void clearWorkspace();
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;


    /** Duplicate selection (Workspace + Gallery; MainWindow supplies new ids). */
    void duplicateSelected(const QVector<SessionImageId> &newIds, int firstSessionIndex);

    bool tryMousePressWorkspaceChrome(QMouseEvent *event);
    bool tryMousePressWorkspaceRotate(QMouseEvent *event);
    bool tryMouseMoveWorkspaceRotate(QMouseEvent *event);
    void updateMouseMoveWorkspaceChromeHover(QMouseEvent *event);
    bool tryMouseReleaseWorkspaceRotate(QMouseEvent *event);

    // Multi-select group scale/rotate (owns GroupTransformSession).
    GroupTransformSession &groupSession() { return m_groupXform; }
    const GroupTransformSession &groupSession() const { return m_groupXform; }
    void clearGroupTransform() { m_groupXform.clear(); }

    int groupHandleAt(const QPoint &viewPos, const QList<ImageItem *> &items) const;
    bool beginGroupScale(int handle, const QList<ImageItem *> &items);
    void updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void endGroupScale();
    void paintGroupSelectionChrome(QPainter *painter, const QList<ImageItem *> &items) const;

    /** Group scale/rotate + single-item handle drag (move phase). */
    bool tryMouseMoveGroupAndHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseGroupDrag(QMouseEvent *event);
    bool tryMouseReleaseHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseItemDrag(QMouseEvent *event);

    // Print page-guide overlay (owns PageGuideSession).
    PageGuideSession &pageGuideSession() { return m_pageGuide; }
    const PageGuideSession &pageGuideSession() const { return m_pageGuide; }

    void setPageGuideVisible(bool on);
    void setPageGuideFromPrinter(const QPrinter &printer);
    QRectF pageGuideSceneRect() const;
    void fitPageGuideToContent(qreal marginPx);
    void setPageGuideSelected(bool on);
    int pageGuideHandleAt(const QPoint &viewPos) const;
    bool beginPageGuideResize(int handle);
    void updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    QRectF pageGuideRectFromHandleDrag(const QPointF &scenePos,
                                      Qt::KeyboardModifiers mods) const;
    void endPageGuideResize();
    void paintPageGuideHandles(QPainter *painter) const;
    static qreal pageGuidePxPerMm();

    /** Page-guide resize drag + hover cursor. */
    bool tryMouseMovePageGuide(QMouseEvent *event);
    bool tryMouseReleasePageGuide(QMouseEvent *event);

private:

    // Free-form restore helper (no external callers)
    void restoreFreeFormStates();

    ImageView *m_view = nullptr;
    Tool m_tool = Tool::Select;

    QList<WorkspaceItemState> m_savedItems;
    QList<ImageItem *> m_stashedItems;
    QTransform m_stashedViewTransform;
    QPointF m_stashedViewCenter;
    bool m_hasStashedView = false;
    QTransform m_savedViewTransform;
    QPointF m_savedViewCenter;
    bool m_hasSavedView = false;

    ItemInteractSession m_itemInteract;
    GroupTransformSession m_groupXform;
    PageGuideSession m_pageGuide;

    /** Free-form pose while Gallery layout temporarily packs tiles. */
    QHash<SessionImageId, ItemComponents::Placement> m_freeFormById;
    QHash<QString, ItemComponents::Placement> m_freeFormByPath;
    QTransform m_freeFormViewTransform;
    bool m_hasFreeFormViewTransform = false;
};

#endif // WORKSPACECONTROLLER_H
