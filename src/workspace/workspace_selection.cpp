// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Canvas selection queries and select-by-id/path helpers.
// ImageView keeps thin public routers for MainWindow and controllers.

#include "workspace/workspacecontroller.h"
#include "imageview.h"
#include "imageitem.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QHash>

void WorkspaceController::selectBySessionIndices(const QList<int> &indices)
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    scene->clearSelection();
    for (int idx : indices) {
        if (ImageItem *item = m_view->hostFindItemBySessionIndex(idx)) {
            item->setSelected(true);
        }
    }
}

QList<SessionImageId> WorkspaceController::selectedSessionIds() const
{
    QList<SessionImageId> out;
    for (ImageItem *item : m_view->liveItems()) {
        if (item && item->isSelected() && item->sessionId() != kInvalidSessionImageId) {
            out.append(item->sessionId());
        }
    }
    return out;
}

void WorkspaceController::selectBySessionIds(const QList<SessionImageId> &ids)
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    scene->clearSelection();
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = m_view->findItemBySessionId(id)) {
            item->setSelected(true);
        }
    }
}

void WorkspaceController::selectPathsByOccurrence(const QStringList &paths)
{
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return;
    }
    scene->clearSelection();
    QHash<QString, int> nextOccurrence;
    for (const QString &path : paths) {
        if (path.isEmpty()) {
            continue;
        }
        const int want = nextOccurrence.value(path, 0);
        int seen = 0;
        for (ImageItem *item : m_view->liveItems()) {
            if (!item || item->path() != path) {
                continue;
            }
            if (seen == want) {
                item->setSelected(true);
                nextOccurrence[path] = want + 1;
                break;
            }
            ++seen;
        }
    }
}

QList<ImageItem *> WorkspaceController::transformTargets() const
{
    QList<ImageItem *> out;
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return out;
    }
    for (QGraphicsItem *gi : scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (m_view->isImageMode() || m_view->liveItems().size() == 1) {
        if (!m_view->liveItems().isEmpty()) {
            out.append(m_view->liveItems().first());
        }
    }
    return out;
}

bool WorkspaceController::hasTransformTargets() const
{
    return !transformTargets().isEmpty();
}

bool WorkspaceController::hasSingleCropTarget() const
{
    return transformTargets().size() == 1;
}

QList<int> WorkspaceController::selectedSessionIndices() const
{
    QList<int> out;
    for (ImageItem *item : m_view->liveItems()) {
        if (!item || !item->isSelected()) {
            continue;
        }
        const int idx = m_view->sessionListIndex(item);
        if (idx >= 0) {
            out.append(idx);
        }
    }
    return out;
}

ImageItem *WorkspaceController::primaryItem() const
{
    // Image mode: prefer the tile bound to the current SessionImageId so
    // duplicate paths do not resolve to the wrong live item via first-in-list.
    if (m_view->isImageMode()) {
        const SessionImageId sid = m_view->hostSessionId().currentIdValue();
        if (sid != kInvalidSessionImageId) {
            if (ImageItem *byId = m_view->findItemBySessionId(sid)) {
                return byId;
            }
        }
    }
    if (m_view->liveItems().isEmpty()) {
        return nullptr;
    }
    return m_view->liveItems().first();
}

ImageItem *WorkspaceController::targetItem() const
{
    // Transform targets:
    //   Image → primary (sole) canvas object
    //   Gallery / Workspace → first selected item; Workspace also falls back to
    //   the sole object when the selection is empty
    QGraphicsScene *scene = m_view->canvasScene();
    if (!scene) {
        return m_view->liveItems().isEmpty() ? nullptr : m_view->liveItems().first();
    }
    const QList<QGraphicsItem *> selected = scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            // Selection can briefly hold stale pointers after destroyCanvasItem.
            if (!m_view->liveItems().contains(item) || item->scene() != scene) {
                continue;
            }
            return item;
        }
    }
    if (m_view->isImageMode() || m_view->liveItems().size() == 1) {
        return m_view->liveItems().isEmpty() ? nullptr : m_view->liveItems().first();
    }
    return nullptr;
}

void WorkspaceController::removeCanvasSessionIds(const QList<SessionImageId> &ids)
{
    if (!m_view->isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    QList<ImageItem *> toRemove;
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = m_view->findItemBySessionId(id)) {
            toRemove.append(item);
        }
    }
    if (toRemove.isEmpty()) {
        return;
    }
    m_view->setUpdatesEnabled(false);
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->blockSignals(true);
    }
    for (ImageItem *item : toRemove) {
        // destroyCanvasItem(persistState=true) snapshots via rememberItemState.
        destroyCanvasItem(item);
    }
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->blockSignals(false);
    }
    m_view->setUpdatesEnabled(true);
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
}

void WorkspaceController::placeSessionIdsOnCanvas(const QList<SessionImageId> &ids,
                                                  const QStringList &paths,
                                                  const QList<int> &sessionIndices)
{
    if (!m_view->isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    if (QGraphicsScene *scene = m_view->canvasScene()) {
        scene->clearSelection();
    }
    m_view->hostBindBook().clearSelectIds();
    for (int i = 0; i < ids.size(); ++i) {
        const SessionImageId sid = ids.at(i);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        const QString path = (i < paths.size()) ? paths.at(i) : QString();
        if (path.isEmpty()) {
            continue;
        }
        if (m_view->findItemBySessionId(sid)) {
            continue; // already on canvas
        }
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        m_view->hostBindBook().addSelectId(sid);
        m_view->addImageForSession(path, sid, idx);
    }
    emit m_view->statusChanged();
    emit m_view->workspacePathsChanged();
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}
