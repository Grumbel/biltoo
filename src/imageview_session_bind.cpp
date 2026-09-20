// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Pending session binds, session-id canvas membership, and load-add placement.

#include "imageview.h"
#include "imageitem.h"
#include "imagecache.h"
#include "sessionappearance.h"
#include "sessionbindbook.h"
#include "viewtransform.h"

#include <QHash>
#include <QSet>
#include <QTimer>
#include <QScrollBar>

bool ImageView::hasPendingSessionBindForPath(const QString &path) const
{
    return m_bindBook.hasBindForPath(path);
}


int ImageView::countPendingSessionBinds(const QString &path) const
{
    return m_bindBook.countBindsForPath(path);
}


void ImageView::purgeSatisfiedPendingBinds(const QString &path)
{
    for (int bi = m_bindBook.bindCount() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_bindBook.bindAt(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_bindBook.removeBindAt(bi);
        }
    }
}


bool ImageView::takePendingSessionBind(const QString &path, PendingSessionBind *out)
{
    return m_bindBook.takeBind(path, out);
}


void ImageView::applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound)
{
    if (!item || !bound.hasScenePos) {
        return;
    }
    // Explicit drop pose: free-form identity placement only (never revive
    // gallery pack scale/cell or a prior non-uniform footprint scale).
    item->setGalleryCellSize({});
    item->setPos(bound.scenePos);
    item->setItemScale(1.0, 1.0);
    item->setItemRotation(0.0);
    item->setItemShear(0.0);
    item->setItemOpacity(1.0);
    item->setItemHFlip(false);
    item->setItemVFlip(false);
    item->setStackZ(m_items.size() - 1);
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    }
    m_displayPipeline.loadGate().removePendingScenePos(item->path());
    rememberItemState(item);
}


bool ImageView::installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image)
{
    if (!item || image.isNull()) {
        return false;
    }
    // Soft mis-labeled as decoded must still accept a stricter long edge.
    if (item->hasDecodedPixels()
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(image))) {
        return false;
    }
    // Drop / LoadAdd placeholder → full. Never non-uniform scale: that stretched
    // oriented (or just aspect-correct) pixels into the provisional footprint and
    // looked like "wrong rotation + stretch" on drag-drop without any rotate.
    const QSize before = item->imageSize();
    const qreal sx0 = item->itemScaleX();
    const qreal sy0 = item->itemScaleY() > 0.0 ? item->itemScaleY() : sx0;
    const qreal footW = before.width() * sx0;
    const qreal footH = before.height() * sy0;
    // Leave Gallery pack geometry on Workspace tiles.
    item->setGalleryCellSize({});
    installDisplayPixels(item, image, SessionAppearance::PixelKind::FullSource,
                         item->sessionId());
    const QSize after = item->imageSize();
    const bool grew = before.isValid() && after.isValid()
        && (before.width() != after.width() || before.height() != after.height());
    if (grew && isWorkspaceMode() && m_layout.isFreeForm()
        && after.width() > 0 && after.height() > 0) {
        const bool neutralScale =
            qAbs(sx0 - 1.0) < 1e-6 && qAbs(sy0 - 1.0) < 1e-6;
        if (neutralScale) {
            // Fresh drop: 1:1 scene units = content pixels (no footprint squash).
            item->setItemScale(1.0, 1.0);
        } else {
            // Prior intentional scale (e.g. moved from Gallery): fit uniformly.
            const qreal s = ViewTransform::uniformFitScale(
                footW, footH, qreal(after.width()), qreal(after.height()));
            if (s > 1e-6) {
                item->setItemScale(s, s);
            }
        }
        return true;
    }
    if (grew) {
        return true;
    }
    item->update();
    return false;
}


bool ImageView::takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                                 PendingSessionBind *out)
{
    if (!out || path.isEmpty() || !item) {
        return false;
    }
    for (int bi = 0; bi < m_bindBook.bindCount(); ++bi) {
        if (m_bindBook.bindAt(bi).path != path) {
            continue;
        }
        const PendingSessionBind candidate = m_bindBook.bindAt(bi);
        if (candidate.id != kInvalidSessionImageId) {
            if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                if (owner != item) {
                    m_bindBook.removeBindAt(bi);
                    --bi;
                    continue;
                }
            }
        }
        m_bindBook.takeBindAt(bi, out);
        if (out->id != kInvalidSessionImageId) {
            item->setSessionId(out->id);
        }
        if (out->index >= 0 && out->id != kInvalidSessionImageId) {
            item->setSessionIndex(out->index);
        }
        return true;
    }
    return false;
}


void ImageView::placeNewLoadAddItem(ImageItem *item, const QString &path,
                                    const QImage &image, bool haveBound,
                                    const PendingSessionBind &bound)
{
    if (!item) {
        return;
    }
    if (isGalleryMode()) {
        // Packed layout owns pose; keep item transform neutral.
        item->setItemRotation(0.0);
        item->setItemShear(0.0);
        item->setItemHFlip(false);
        item->setItemVFlip(false);
        item->setItemOpacity(1.0);
        return;
    }
    if (haveBound && bound.hasScenePos) {
        // Explicit drop: place at the drop point (new placement).
        applyPendingBindScenePos(item, bound);
        return;
    }
    if (haveBound && bound.id != kInvalidSessionImageId
        && appearance().get(bound.id)) {
        // Thumbnail membership toggle: restore last Workspace pose.
        applyState(item, *appearance().get(bound.id));
        return;
    }
    QPointF pos;
    if (m_displayPipeline.loadGate().takePendingScenePos(path, &pos)) {
        item->setPos(pos);
        item->setItemScale(1.0);
        item->setItemRotation(0.0);
        item->setItemOpacity(1.0);
        item->setStackZ(m_items.size() - 1);
        return;
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
        applyState(item, *st);
        return;
    }
    WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
    const QSizeF sz(image.width(), image.height());
    s.pos = findEmptyPlacement(sz);
    applyState(item, s);
}


QList<ImageItem *> ImageView::collectItemsForSessionId(SessionImageId sessionId) const
{
    QList<ImageItem *> doomed;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *item : list) {
            if (item && item->sessionId() == sessionId && !doomed.contains(item)) {
                doomed.append(item);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());
    return doomed;
}


QStringList ImageView::destroySessionIdItems(const QList<ImageItem *> &doomed)
{
    QStringList removedPaths;
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        removedPaths.append(path);
        // Drop in-flight decodes so a late LoadAdd cannot create a tile or
        // call applyLayout after this session image is gone.
        m_displayPipeline.loadGate().removePendingWorkspacePath(path);
        gallerySoftResetPath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        m_bindBook.removeIndexForPath(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        destroyCanvasItem(item);
    }
    return removedPaths;
}


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
    for (int i = 0; i < pathOrderSize(); ++i) {
        const QString &path = pathOrderPathAt(i);
        const SessionImageId sid = pathOrderIdAt(i);
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
            applyLayout(GalleryPackReason::SessionMutate);
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
    appearance().remove(sessionId);

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
        ensureAttentionPoint();
        if (viewport()) {
            viewport()->update();
        }
    }
}


bool ImageView::hasWorkspaceSessionIndex(int sessionIndex) const
{
    return findItemBySessionIndex(sessionIndex) != nullptr;
}


void ImageView::removeWorkspaceSessionIndex(int sessionIndex)
{
    ImageItem *item = findItemBySessionIndex(sessionIndex);
    if (!item) {
        return;
    }
    // Prefer id-based detach when the tile is bound (duplicate-safe).
    if (item->sessionId() != kInvalidSessionImageId) {
        detachCanvasSessionId(item->sessionId());
        return;
    }
    const QString path = item->path();
    // Only cancel pending work if no other live tile still uses this path.
    bool pathStillLive = false;
    for (ImageItem *other : m_items) {
        if (other && other != item && other->path() == path) {
            pathStillLive = true;
            break;
        }
    }
    if (!pathStillLive) {
        m_displayPipeline.loadGate().removePendingWorkspacePath(path);
        m_displayPipeline.loadGate().removePendingScenePos(path);
        m_bindBook.removeIndexForPath(path);
        gallerySoftResetPath(path);
}
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}


void ImageView::detachCanvasSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    // Canvas membership only — keep session appearance and session list entry.
    QList<ImageItem *> doomed;
    for (ImageItem *item : m_items) {
        if (item && item->sessionId() == sessionId) {
            doomed.append(item);
        }
    }
    for (ImageItem *item : doomed) {
        const QString path = item->path();
        bool pathStillLive = false;
        for (ImageItem *other : m_items) {
            if (other && other != item && other->path() == path) {
                pathStillLive = true;
                break;
            }
        }
        if (!pathStillLive) {
            takePendingWorkspacePath(path);
            m_displayPipeline.loadGate().removePendingScenePos(path);
            m_bindBook.removeIndexForPath(path);
        gallerySoftResetPath(path);
}
        // Drop pending binds for this id only (not every same-path bind).
        m_bindBook.removeBindsForSessionId(sessionId);
        destroyCanvasItem(item);
    }
    if (!doomed.isEmpty()) {
        emit statusChanged();
        emit workspacePathsChanged();
    }
}


void ImageView::bindSelectedSessionIndices(int firstSessionIndex)
{
    if (firstSessionIndex < 0) {
        return;
    }
    int next = firstSessionIndex;
    for (ImageItem *item : m_items) {
        if (item->isSelected()) {
            item->setSessionIndex(next);
            ++next;
        }
    }
}


void ImageView::bindSelectedSessionIds(const QList<SessionImageId> &ids)
{
    int i = 0;
    for (ImageItem *item : m_items) {
        if (!item->isSelected()) {
            continue;
        }
        if (i >= ids.size()) {
            break;
        }
        const SessionImageId id = ids.at(i++);
        if (id == kInvalidSessionImageId) {
            continue;
        }
        // Never give the same SessionImageId to two live tiles (drop path used
        // to stamp {sid} onto every selected item while LoadAdd also bound it).
        if (ImageItem *owner = findItemBySessionId(id)) {
            if (owner != item) {
                qCritical("bindSelectedSessionIds: SessionImageId %lld already on another tile — skip",
                          static_cast<long long>(id));
                continue;
            }
        }
        WorkspaceItemState slot;
        if (m_pendingAppearance.take(item, &slot)) {
            // Pending may carry colour grade from Duplicate before the live item
            // was fully synced — apply it so the tile and sessionAppearanceImage match.
            item->setColorAdjustments(slot.colorAdjust);
        } else {
            slot = captureState(item);
        }
        item->setSessionId(id);
        // Live placement from the canvas item (Duplicate offsets, scales, …).
        slot.pos = item->pos();
        slot.scale = item->itemScaleX();
        slot.scaleY = item->itemScaleY();
        slot.shear = item->itemShear();
        slot.rotation = item->itemRotation();
        slot.opacity = item->itemOpacity();
        slot.z = item->stackZ();
        slot.hFlip = item->itemHFlip();
        slot.vFlip = item->itemVFlip();
        slot.hasCrop = item->sessionHasCrop();
        slot.cropRect = item->sessionCropRect();
        slot.contentHFlip = item->contentHFlip();
        slot.contentVFlip = item->contentVFlip();
        slot.colorAdjust = item->colorAdjustments();
        slot.sessionId = id;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        appearance().set(id, slot);
        // Drive ThumbnailBar per-id override (cropped/rotated/graded pixels).
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(id, item->path(), appearance);
            emit sessionCropApplied(id, item->path(), appearance, item->sessionHasCrop());
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
        rememberItemState(item);
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

