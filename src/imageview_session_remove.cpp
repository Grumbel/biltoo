// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"
#include "session/packorderview.h"
#include "imageitem.h"
#include "itemcomponents.h"
#include "imagecache.h"
#include "session/sessionappearance.h"
#include "session/sessionbindbook.h"
#include "viewtransform.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QScrollBar>

void ImageView::prunePendingBindsAndSavedForSessionId(SessionImageId sessionId)
{
    m_bindBook.removeBindsForSessionId(sessionId);
    for (int i = m_workspace.savedItems().size() - 1; i >= 0; --i) {
        if (m_workspace.savedItems().at(i).sessionId == sessionId) {
            m_workspace.savedItems().removeAt(i);
        }
    }
}


void ImageView::prunePathOrdersAfterSessionRemove(const QStringList &removedPaths)
{
    if (removedPaths.isEmpty()) {
        return;
    }
    // Rebuild path/id order aligned with remaining tiles (IDENTITY: id first).
    QStringList prunedPaths;
    QVector<SessionImageId> prunedIds;
    prunedPaths.reserve(m_items.size());
    prunedIds.reserve(m_items.size());
    QSet<SessionImageId> seenIds;
    QHash<QString, int> unboundBudget;
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            if (seenIds.contains(sid)) {
                continue;
            }
            seenIds.insert(sid);
            prunedPaths.append(item->path());
            prunedIds.append(sid);
        } else {
            unboundBudget[item->path()] += 1;
        }
    }
    // Preserve prior order for unbound path slots still live.
    const PackOrderView pack = currentPackOrder();
    for (int i = 0; i < pack.size(); ++i) {
        const QString path = pack.pathAt(i);
        const SessionImageId sid = pack.idAt(i);
        if (sid != kInvalidSessionImageId) {
            continue; // already taken from live bound tiles
        }
        if (unboundBudget.value(path) > 0) {
            prunedPaths.append(path);
            prunedIds.append(kInvalidSessionImageId);
            unboundBudget[path] -= 1;
        }
    }
    pathOrderSetOrder(prunedPaths, prunedIds);
}


void ImageView::restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                                  const QPointF &keptCenter, int scrollH, int scrollV)
{
    // Gallery: repack so deleted tiles do not leave empty holes. Preserve the
    // pre-delete viewport centre afterward (same idea as return-from-Image).
    if (gallery && m_scene) {
        if (!m_items.isEmpty()) {
            m_gallery.applyLayout(GalleryPackReason::SessionMutate);
        } else if (keptSceneRect.isValid()) {
            m_scene->setSceneRect(keptSceneRect);
        }
        if (!keptCenter.isNull()) {
            centerOn(keptCenter);
        }
        if (horizontalScrollBar()) {
            horizontalScrollBar()->setValue(scrollH);
        }
        if (verticalScrollBar()) {
            verticalScrollBar()->setValue(scrollV);
        }
        m_gallery.setViewportSnapshot(keptCenter, scrollH, scrollV);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
}


void ImageView::removeWorkspaceSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    m_itemWorld.removeAppearance(sessionId);

    // Capture view before any item is destroyed — Qt may shrink sceneRect
    // while removeItem runs, which zeroes scrollbar ranges mid-loop.
    const bool gallery = isGalleryMode();
    QRectF keptSceneRect = (m_scene && gallery) ? m_scene->sceneRect() : QRectF();
    if (gallery && m_scene && !keptSceneRect.isValid()) {
        keptSceneRect = m_scene->itemsBoundingRect();
        if (keptSceneRect.isValid()) {
            keptSceneRect.adjust(-64, -64, 64, 64);
        }
    }
    const QPointF keptCenter = gallery
        ? mapToScene(viewport()->rect().center())
        : QPointF();
    const int scrollH = horizontalScrollBar() ? horizontalScrollBar()->value() : 0;
    const int scrollV = verticalScrollBar() ? verticalScrollBar()->value() : 0;

    // Collect first — destroyCanvasItem mutates m_items / stashes.
    const QList<ImageItem *> doomed = collectItemsForSessionId(sessionId);
    const QStringList removedPaths = destroySessionIdItems(doomed);
    prunePendingBindsAndSavedForSessionId(sessionId);
    prunePathOrdersAfterSessionRemove(removedPaths);
    restoreViewportAfterSessionRemove(gallery, keptSceneRect, keptCenter, scrollH, scrollV);

    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::setCurrentSessionId(SessionImageId id)
{
    if (m_sessionId.currentId == id) {
        return;
    }
    m_sessionId.setCurrentId(id);
    // Attention marker is per SessionImageId — reload draft for the new image.
    if (m_attentionCtrl.session().active()) {
        m_attentionCtrl.session().clearDraft();
        m_attentionCtrl.ensureAttentionPoint();
        if (viewport()) {
            viewport()->update();
        }
    }
}


void ImageView::removeCanvasSessionIds(const QList<SessionImageId> &ids)
{
    if (!isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    QList<ImageItem *> toRemove;
    for (SessionImageId id : ids) {
        if (id == kInvalidSessionImageId) {
            continue;
        }
        if (ImageItem *item = findItemBySessionId(id)) {
            toRemove.append(item);
        }
    }
    if (toRemove.isEmpty()) {
        return;
    }
    setUpdatesEnabled(false);
    if (m_scene) {
        m_scene->blockSignals(true);
    }
    for (ImageItem *item : toRemove) {
        // destroyCanvasItem(persistState=true) snapshots via rememberItemState.
        destroyCanvasItem(item);
    }
    if (m_scene) {
        m_scene->blockSignals(false);
    }
    setUpdatesEnabled(true);
    viewport()->update();
    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::placeSessionIdsOnCanvas(const QList<SessionImageId> &ids,
                                        const QStringList &paths,
                                        const QList<int> &sessionIndices)
{
    if (!isWorkspaceMode() || ids.isEmpty()) {
        return;
    }
    if (m_scene) {
        m_scene->clearSelection();
    }
    m_bindBook.clearSelectIds();
    for (int i = 0; i < ids.size(); ++i) {
        const SessionImageId sid = ids.at(i);
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        const QString path = (i < paths.size()) ? paths.at(i) : QString();
        if (path.isEmpty()) {
            continue;
        }
        if (findItemBySessionId(sid)) {
            continue; // already on canvas
        }
        const int idx = (i < sessionIndices.size()) ? sessionIndices.at(i) : -1;
        m_bindBook.addSelectId(sid);
        addImageForSession(path, sid, idx);
    }
    emit statusChanged();
    emit workspacePathsChanged();
    viewport()->update();
}

