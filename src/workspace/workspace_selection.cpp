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
