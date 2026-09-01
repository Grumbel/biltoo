// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imageloader.h"

#include <QSet>

void ImageView::setWorkspacePaths(const QStringList &paths)
{
    setWorkspacePaths(paths, {});
}

void ImageView::setWorkspacePaths(const QStringList &paths,
                                  const QVector<SessionImageId> &sessionIds)
{
    if (isImageMode()) {
        return;
    }

    QSet<QString> wanted(paths.begin(), paths.end());
    for (int i = m_items.size() - 1; i >= 0; --i) {
        ImageItem *item = m_items.at(i);
        if (!wanted.contains(item->path())) {
            m_galleryDecodeScheduled.remove(item->path());
            m_galleryDecodeFailed.remove(item->path());
            destroyCanvasItem(item);
        }
    }

    m_pathOrder = paths;

    const bool virtualize = isGalleryMode() && paths.size() >= kGalleryVirtualThreshold;
    const bool haveIds = !sessionIds.isEmpty();

    for (int i = 0; i < paths.size(); ++i) {
        const QString &path = paths.at(i);
        const SessionImageId sid = (haveIds && i < sessionIds.size())
            ? sessionIds.at(i)
            : kInvalidSessionImageId;

        if (ImageItem *existing = findItemByPath(path)) {
            // Bind session identity so Image-mode edits can peer-sync this tile.
            const bool newlyBoundId = (sid != kInvalidSessionImageId
                                      && existing->sessionId() == kInvalidSessionImageId);
            if (newlyBoundId) {
                existing->setSessionId(sid);
            }
            if (existing->sessionIndex() < 0) {
                existing->setSessionIndex(i);
            }
            // If this tile never had an id while Image-mode edits ran, peer sync
            // could not update it. Force a full re-decode so LoadAdd applies
            // m_appearance for this id (safe; pixels must be on-disk full).
            if (newlyBoundId && existing->hasDecodedPixels()
                && sid != kInvalidSessionImageId) {
                const WorkspaceItemState *appPtr = m_appearance.get(sid);
                if (!appPtr) {
                    continue;
                }
                const WorkspaceItemState &app = *appPtr;
                if (app.hasCrop || app.contentHFlip || app.contentVFlip
                    || app.contentQuarterTurns != 0) {
                    existing->clearDecodedPixels();
                    m_galleryDecodeScheduled.remove(path);
                    m_pendingWorkspacePaths.remove(path);
                    m_galleryDecodeFailed.remove(path);
                    PendingSessionBind b;
                    b.path = path;
                    b.id = sid;
                    b.index = i;
                    m_pendingSessionBinds.append(b);
                    if (isGalleryMode()) {
                        scheduleGalleryDecode(path);
                    } else {
                        scheduleImageLoad(path, LoadAdd);
                    }
                }
            }
            continue;
        }

        if (sid != kInvalidSessionImageId || i >= 0) {
            PendingSessionBind b;
            b.path = path;
            b.id = sid;
            b.index = i;
            m_pendingSessionBinds.append(b);
            m_pendingSessionIndexByPath.insert(path, i);
        }

        if (virtualize) {
            // Fast size probe + placeholder; full decode is viewport-windowed.
            ImageItem *ph = createPlaceholderItem(path, probeImageSize(path));
            if (ph) {
                if (sid != kInvalidSessionImageId) {
                    ph->setSessionId(sid);
                }
                ph->setSessionIndex(i);
            }
        } else {
            scheduleImageLoad(path, LoadAdd);
        }
    }

    // Keep canvas order aligned with session/sort order (not async load order).
    reorderItemsByPaths(m_pathOrder);

    if (haveIds) {
        rebindWorkspaceSession(paths, sessionIds);
    }

    // Workspace: seed a selection if empty. Gallery must not steal focus to
    // "last item" on layout switch / path refresh (preserves multi-select).
    if (isWorkspaceMode() && m_scene->selectedItems().isEmpty() && !m_items.isEmpty()) {
        m_items.last()->setSelected(true);
    }

    if (isGalleryMode() && !m_items.isEmpty()) {
        applyLayout(GalleryPackReason::EnterGallery);
        updateGalleryDecodeWindow();
    }

    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::reorderItemsByPaths(const QStringList &paths)
{
    if (m_items.isEmpty() || paths.isEmpty()) {
        return;
    }
    QList<ImageItem *> ordered;
    ordered.reserve(m_items.size());
    QSet<ImageItem *> seen;
    for (const QString &path : paths) {
        if (ImageItem *item = findItemByPath(path)) {
            ordered.append(item);
            seen.insert(item);
        }
    }
    for (ImageItem *item : m_items) {
        if (!seen.contains(item)) {
            ordered.append(item);
        }
    }
    if (ordered != m_items) {
        m_items = ordered;
        for (int i = 0; i < m_items.size(); ++i) {
            m_items.at(i)->setStackZ(i);
        }
    }
}

void ImageView::removeWorkspacePath(const QString &path)
{
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_pendingWorkspacePaths.remove(path);
    m_pendingScenePos.remove(path);
    m_galleryDecodeScheduled.remove(path);
    m_galleryDecodeFailed.remove(path);
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
}

bool ImageView::addImage(const QString &path)
{
    if (isImageMode()) {
        return false;
    }

    if (ImageItem *existing = findItemByPath(path)) {
        m_scene->clearSelection();
        existing->setSelected(true);
        ensureVisibleItem(existing);
        emit statusChanged();
        return true;
    }

    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageForSession(const QString &path, SessionImageId sessionId,
                                     int sessionIndex)
{
    if (isImageMode() || path.isEmpty()) {
        return false;
    }
    if (sessionId != kInvalidSessionImageId) {
        if (ImageItem *existing = findItemBySessionId(sessionId)) {
            if (existing->scene() == m_scene) {
                m_scene->clearSelection();
                existing->setSelected(true);
                ensureVisibleItem(existing);
                emit statusChanged();
                return true;
            }
        }
    }
    if (sessionIndex >= 0) {
        if (ImageItem *existing = findItemBySessionIndex(sessionIndex)) {
            m_scene->clearSelection();
            existing->setSelected(true);
            ensureVisibleItem(existing);
            emit statusChanged();
            return true;
        }
    }
    // Another session slot may already show this path — clone decoded pixels.
    if (ImageItem *donor = findItemByPath(path)) {
        if (donor->hasDecodedPixels() || !donor->sourceImage().isNull()) {
            ImageItem *copy = createItemFromImage(path, donor->sourceImage(),
                                                  /*applyStoredSessionCrop=*/false);
            if (copy) {
                copy->setSessionId(sessionId);
                copy->setSessionIndex(sessionIndex);
                copy->setItemScale(donor->itemScaleX(), donor->itemScaleY());
                copy->setItemRotation(donor->itemRotation());
                copy->setItemHFlip(donor->itemHFlip());
                copy->setItemVFlip(donor->itemVFlip());
                copy->setItemOpacity(donor->itemOpacity());
                copy->setStackZ(donor->stackZ() + 0.01);
                copy->setPos(donor->pos() + QPointF(40.0, 40.0));
                if (m_scene) {
                    m_scene->clearSelection();
                }
                copy->setSelected(true);
                if (isWorkspaceMode()) {
                    updateWorkspaceSceneRect();
                }
                emit statusChanged();
                emit workspacePathsChanged();
                return true;
            }
        }
    }
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0) {
        PendingSessionBind b;
        b.path = path;
        b.id = sessionId;
        b.index = sessionIndex;
        m_pendingSessionBinds.append(b);
        if (sessionIndex >= 0) {
            m_pendingSessionIndexByPath.insert(path, sessionIndex);
        }
    }
    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::addImageForSession(const QString &path, int sessionIndex)
{
    return addImageForSession(path, kInvalidSessionImageId, sessionIndex);
}

bool ImageView::addImageAt(const QString &path, const QPointF &scenePos)
{
    if (path.isEmpty()) {
        return false;
    }
    m_pendingScenePos.insert(path, scenePos);
    return addImage(path);
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                                     SessionImageId sessionId, int sessionIndex)
{
    if (path.isEmpty() || isImageMode()) {
        return false;
    }
    // Workspace free-form: re-dropping a path that is already on the canvas
    // creates another instance at the drop point (does not move the original).
    if (ImageItem *donor = findItemByPath(path)) {
        if (donor->hasDecodedPixels() || !donor->sourceImage().isNull()) {
            ImageItem *copy = createItemFromImage(path, donor->sourceImage(),
                                                  /*applyStoredSessionCrop=*/false);
            if (copy) {
                copy->setItemScale(donor->itemScaleX(), donor->itemScaleY());
                copy->setItemRotation(donor->itemRotation());
                copy->setItemHFlip(donor->itemHFlip());
                copy->setItemVFlip(donor->itemVFlip());
                copy->setContentHFlip(donor->contentHFlip());
                copy->setContentVFlip(donor->contentVFlip());
                copy->setSessionCrop(donor->sessionHasCrop(), donor->sessionCropRect());
                copy->setItemOpacity(donor->itemOpacity());
                copy->setStackZ(donor->stackZ() + 0.01);
                copy->setPos(scenePos);
                copy->setSessionId(sessionId);
                copy->setSessionIndex(sessionIndex);
                if (m_scene) {
                    m_scene->clearSelection();
                }
                copy->setSelected(true);
                if (sessionId != kInvalidSessionImageId) {
                    WorkspaceItemState slot = captureState(copy);
                    slot.sessionId = sessionId;
                    slot.sessionIndex = sessionIndex;
                    m_appearance.set(sessionId, slot);
                }
                updateWorkspaceSceneRect();
                ensureVisibleItem(copy);
                emit statusChanged();
                emit workspacePathsChanged();
                return true;
            }
        }
    }
    m_pendingScenePos.insert(path, scenePos);
    if (sessionId != kInvalidSessionImageId || sessionIndex >= 0) {
        PendingSessionBind b;
        b.path = path;
        b.id = sessionId;
        b.index = sessionIndex;
        m_pendingSessionBinds.append(b);
    }
    scheduleImageLoad(path, LoadAdd);
    emit statusChanged();
    return true;
}

bool ImageView::placeOrMoveImageAt(const QString &path, const QPointF &scenePos)
{
    return placeOrMoveImageAt(path, scenePos, kInvalidSessionImageId, -1);
}

void ImageView::selectBySessionIndices(const QList<int> &indices)
{
    if (!m_scene) {
        return;
    }
    m_scene->clearSelection();
    for (int idx : indices) {
        if (ImageItem *item = findItemBySessionIndex(idx)) {
            item->setSelected(true);
        }
    }
}

void ImageView::rebindWorkspaceSessionIndices(const QStringList &sessionFiles)
{
    // Legacy path-only rebind (no stable ids available).
    rebindWorkspaceSession(sessionFiles, {});
}

void ImageView::rebindWorkspaceSession(const QStringList &sessionFiles,
                                       const QVector<SessionImageId> &sessionIds)
{
    if (sessionFiles.isEmpty()) {
        for (ImageItem *item : m_items) {
            item->setSessionIndex(-1);
            // Keep sessionId — still identifies the session image if list is rebuilt.
        }
        return;
    }

    QSet<int> usedIndex;
    QSet<SessionImageId> usedId;

    // 1) Prefer stable id: refresh list-order cache (sessionIndex) from id position.
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid != kInvalidSessionImageId) {
            int found = -1;
            for (int i = 0; i < sessionIds.size() && i < sessionFiles.size(); ++i) {
                if (sessionIds.at(i) == sid) {
                    found = i;
                    break;
                }
            }
            if (found >= 0 && sessionFiles.at(found) == item->path()) {
                item->setSessionIndex(found);
                usedIndex.insert(found);
                usedId.insert(sid);
                continue;
            }
            // Id not in current session list — unbound from list order.
            item->setSessionIndex(-1);
            continue;
        }
        // No id yet — validate legacy index.
        const int si = item->sessionIndex();
        if (si >= 0 && si < sessionFiles.size()
            && sessionFiles.at(si) == item->path() && !usedIndex.contains(si)) {
            usedIndex.insert(si);
            if (si < sessionIds.size()) {
                item->setSessionId(sessionIds.at(si));
                usedId.insert(sessionIds.at(si));
            }
        } else {
            item->setSessionIndex(-1);
        }
    }

    // 2) Assign remaining session rows to unbound canvas items by path occurrence.
    for (int i = 0; i < sessionFiles.size(); ++i) {
        if (usedIndex.contains(i)) {
            continue;
        }
        const QString &path = sessionFiles.at(i);
        const SessionImageId sid = (i < sessionIds.size()) ? sessionIds.at(i)
                                                           : kInvalidSessionImageId;
        for (ImageItem *item : m_items) {
            if (!item || item->sessionIndex() >= 0) {
                continue;
            }
            if (item->path() != path) {
                continue;
            }
            // Do not steal an item that already has a different stable id.
            if (item->sessionId() != kInvalidSessionImageId
                && sid != kInvalidSessionImageId
                && item->sessionId() != sid) {
                continue;
            }
            item->setSessionIndex(i);
            if (sid != kInvalidSessionImageId) {
                item->setSessionId(sid);
            }
            usedIndex.insert(i);
            break;
        }
    }
}

ImageItem *ImageView::primaryItem() const
{
    if (m_items.isEmpty()) {
        return nullptr;
    }
    return m_items.first();
}

ImageItem *ImageView::targetItem() const
{
    // Transform targets:
    //   Image → primary (sole) canvas object
    //   Gallery / Workspace → first selected item; Workspace also falls back to
    //   the sole object when the selection is empty
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    for (QGraphicsItem *gi : selected) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            return item;
        }
    }
    if (isImageMode() || m_items.size() == 1) {
        return m_items.isEmpty() ? nullptr : m_items.first();
    }
    return nullptr;
}

bool ImageView::hasTransformTargets() const
{
    return !transformTargets().isEmpty();
}

bool ImageView::hasSingleCropTarget() const
{
    return transformTargets().size() == 1;
}
