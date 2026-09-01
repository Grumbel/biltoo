// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "gallerylayout.h"
#include "imageitem.h"
#include "imageloader.h"
#include "sessionappearance.h"

#include <QHash>
#include <QPrinter>
#include <QPageLayout>
#include <QPageSize>

#include <QUndoCommand>
#include <QUndoStack>

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>
#include <QPaintEvent>
#include <QScrollBar>
#include <QTimer>
#include <QSet>
#include <QThreadPool>
#include <QVector>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QWheelEvent>
#include <QtMath>
#include <cmath>
#include <algorithm>

void ImageView::applyItemModeFlags(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Strict separation:
    //   Workspace → movable + handles
    //   Gallery   → selectable only (open on click), no chrome
    //   Image     → static, no selection chrome
    if (isWorkspaceMode()) {
        item->setInteractive(true);
        item->setScaleHandlesEnabled(true);
    } else if (isGalleryMode()) {
        item->setGallerySelectable(true);
        item->setScaleHandlesEnabled(false);
    } else {
        item->setGalleryCellSize({});
        item->setInteractive(false);
        item->setScaleHandlesEnabled(false);
    }
}

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.sessionId = item->sessionId();
    s.sessionIndex = item->sessionIndex(); // order cache only
    s.pos = item->pos();
    s.scale = item->itemScaleX();
    s.scaleY = item->itemScaleY();
    s.rotation = item->itemRotation(); // placement only
    s.orientation = 0.0;
    s.opacity = item->itemOpacity();
    s.z = item->stackZ();
    s.hFlip = item->itemHFlip();
    s.vFlip = item->itemVFlip();
    // Live item is authoritative for per-instance appearance (crop + content
    // flips). Do not overwrite from the path map — that was clearing content
    // flags for unbound Workspace items after bakeFlip/bakeRotate90.
    s.hasCrop = item->sessionHasCrop();
    s.cropRect = item->sessionCropRect();
    s.contentHFlip = item->contentHFlip();
    s.contentVFlip = item->contentVFlip();
    // Quarter turns live only on the path map (not stored on ImageItem).
    const auto prev = m_itemStates.constFind(item->path());
    if (prev != m_itemStates.cend()) {
        s.contentQuarterTurns = prev->contentQuarterTurns;
        if (s.sessionIndex < 0 && prev->sessionIndex >= 0) {
            // Keep a path-level session index hint when the item is unbound.
            s.sessionIndex = prev->sessionIndex;
        }
    }
    return s;
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale, state.scaleY > 0.0 ? state.scaleY : state.scale);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setStackZ(state.z);
    item->setItemHFlip(state.hFlip);
    item->setItemVFlip(state.vFlip);
    // Session crop is applied at decode time (createItemFromImage / setSourceImage),
    // not here — otherwise a second apply would crop the already-cropped pixmap.
}

void ImageView::rememberItemState(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId sid =
        item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);

    // Image mode must not overwrite Workspace placement (pos / scale / free tilt).
    // Bound session images: appearance lives only in m_appearance (Phase 2).
    if (isImageMode()) {
        if (sid != kInvalidSessionImageId) {
            // Leave path-map placement untouched; do not last-write crop/flip by path.
            return;
        }
        // Unbound legacy tile: path map is the only store.
        WorkspaceItemState s;
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            s = *it;
        } else {
            s.path = item->path();
        }
        s.sessionIndex = item->sessionIndex() >= 0 ? item->sessionIndex() : s.sessionIndex;
        s.hFlip = item->itemHFlip();
        s.vFlip = item->itemVFlip();
        s.orientation = 0.0;
        s.hasCrop = item->sessionHasCrop();
        s.cropRect = item->sessionCropRect();
        s.contentHFlip = item->contentHFlip();
        s.contentVFlip = item->contentVFlip();
        if (it != m_itemStates.cend()) {
            s.contentQuarterTurns = it->contentQuarterTurns;
        }
        m_itemStates.insert(item->path(), s);
        return;
    }
    // Workspace / Gallery: path map still holds free placement for this instance.
    // Appearance for bound ids is written in commitItemSessionEdit → m_appearance.
    m_itemStates.insert(item->path(), captureState(item));
}

QImage ImageView::sessionAppearanceImage(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Content 90°/flip/crop are baked into source pixels. Placement rotation is not.
    QImage img = item->sourceImage();
    if (img.isNull()) {
        return {};
    }
    // Legacy live flip flags (should be empty after bake).
    if (item->itemHFlip()) {
        img = img.flipped(Qt::Horizontal);
    }
    if (item->itemVFlip()) {
        img = img.flipped(Qt::Vertical);
    }
    return img;
}



WorkspaceItemState ImageView::sessionAppearanceValue(SessionImageId id) const
{
    if (id == kInvalidSessionImageId) {
        return {};
    }
    return m_appearance.value(id);
}

bool ImageView::hasSessionAppearance(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_appearance.contains(id);
}

void ImageView::setSessionAppearance(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    m_appearance.set(id, state);
}

void ImageView::applyContentBakes(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Order: flips then quarter turns (matches bakeFlip / bakeRotate90 live order).
    if (state.contentHFlip || state.contentVFlip) {
        item->bakeFlip(state.contentHFlip, state.contentVFlip);
    }
    if (state.contentQuarterTurns != 0) {
        item->bakeRotate90(state.contentQuarterTurns);
    }
    // Keep chrome indicators in sync with session state (bakeFlip clears display flags).
    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
}

void ImageView::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    if (!item || quarterTurns == 0) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureState(item);
    beforeSt.hasCrop = item->sessionHasCrop();
    beforeSt.cropRect = item->sessionCropRect();
    beforeSt.contentHFlip = item->contentHFlip();
    beforeSt.contentVFlip = item->contentVFlip();
    {
        const SessionImageId sid0 = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
        if (sid0 != kInvalidSessionImageId) {
            if (const WorkspaceItemState *it = m_appearance.get(sid0)) {
                beforeSt.contentQuarterTurns = it->contentQuarterTurns;
                beforeSt.sessionId = sid0;
            }
        }
    }

    item->bakeRotate90(quarterTurns);
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_currentSessionId;
    }
    int prevTurns = beforeSt.contentQuarterTurns;
    int turns = (prevTurns + quarterTurns) % 4;
    if (turns < 0) {
        turns += 4;
    }
    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState s = captureState(item);
        s.sessionId = sid;
        s.contentQuarterTurns = turns;
        s.orientation = 0.0;
        s.hasCrop = item->sessionHasCrop();
        s.cropRect = item->sessionCropRect();
        s.contentHFlip = item->contentHFlip();
        s.contentVFlip = item->contentVFlip();
        m_appearance.set(sid, s);
    }
    commitItemSessionEdit(item);

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.contentHFlip = item->contentHFlip();
    afterSt.contentVFlip = item->contentVFlip();
    afterSt.contentQuarterTurns = turns;
    afterSt.sessionId = sid;
    pushItemContentCommand(tr("Rotate"), item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}

void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    if (!item || (!horizontal && !vertical)) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureState(item);
    beforeSt.hasCrop = item->sessionHasCrop();
    beforeSt.cropRect = item->sessionCropRect();
    beforeSt.contentHFlip = item->contentHFlip();
    beforeSt.contentVFlip = item->contentVFlip();
    {
        const SessionImageId sid0 = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_currentSessionId : kInvalidSessionImageId);
        if (sid0 != kInvalidSessionImageId) {
            if (const WorkspaceItemState *it = m_appearance.get(sid0)) {
                beforeSt.contentQuarterTurns = it->contentQuarterTurns;
                beforeSt.sessionId = sid0;
            }
        }
    }

    item->bakeFlip(horizontal, vertical);
    // Toggle against this item's content flags only — never path-keyed state
    // (duplicates sharing a path would otherwise share one flip bit).
    bool h = item->contentHFlip();
    bool v = item->contentVFlip();
    if (horizontal) {
        h = !h;
    }
    if (vertical) {
        v = !v;
    }
    item->setContentHFlip(h);
    item->setContentVFlip(v);
    commitItemSessionEdit(item);

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.contentHFlip = h;
    afterSt.contentVFlip = v;
    afterSt.contentQuarterTurns = beforeSt.contentQuarterTurns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(horizontal && !vertical ? tr("Flip horizontal")
                          : vertical && !horizontal ? tr("Flip vertical")
                          : tr("Flip"),
                           item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}

void ImageView::commitItemSessionEdit(ImageItem *item)
{
    if (!item) {
        return;
    }
    rememberItemState(item);

    // Per-session-image appearance is a value copy keyed by stable id.
    {
        SessionImageId sid = item->sessionId();
        // Image mode may bind the cursor id when the live item is not yet tagged.
        // Workspace/Gallery must not invent an id — that merges edits onto peers.
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_currentSessionId;
        }
        if (sid != kInvalidSessionImageId) {
            if (item->sessionId() == kInvalidSessionImageId) {
                item->setSessionId(sid);
            }
            WorkspaceItemState slot = captureState(item);
            slot.sessionId = sid;
            slot.sessionIndex = item->sessionIndex();
            slot.path = item->path();
            if (const WorkspaceItemState *prev = m_appearance.get(sid)) {
                if (slot.contentQuarterTurns == 0
                    && prev->contentQuarterTurns != 0) {
                    slot.contentQuarterTurns = prev->contentQuarterTurns;
                }
            }
            m_appearance.set(sid, slot);
            // Bound: do not last-write appearance onto the path map (duplicates
            // share a path). Placement remains in m_itemStates from Workspace
            // rememberItemState / snapshot only.
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, item->path(), appearance);
                emit sessionCropApplied(sid, item->path(), appearance);
                emit sessionAppearanceChanged(item->path(), appearance);
                emit sessionCropApplied(item->path(), appearance);
            }
        }
    }

    // Propagate pixel / flip / orientation session edits to matching canvas and
    // stashed instances. Placement (pos, scale, free tilt) is preserved.
    const QString path = item->path();
    // Strict identity: only a valid SessionImageId. Never m_currentSessionId
    // fallback here — that would push this item's pixels onto another tile.
    const SessionImageId sessionId = item->sessionId();
    const QImage src = item->sourceImage();
    const bool hFlip = item->itemHFlip();
    const bool vFlip = item->itemVFlip();
    const bool contentH = item->contentHFlip();
    const bool contentV = item->contentVFlip();

    QList<ImageItem *> peers;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *other : list) {
            if (other && other != item) {
                peers.append(other);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());

    auto shouldSync = [&](ImageItem *other) -> bool {
        if (!other || other == item) {
            return false;
        }
        // Same stable session-image id only. Path / list-index must never merge
        // independent duplicates on the Workspace.
        return sessionId != kInvalidSessionImageId
            && other->sessionId() == sessionId;
    };

    auto syncOne = [&](ImageItem *other) {
        if (!shouldSync(other)) {
            return;
        }
        if (!src.isNull()) {
            other->setSourceImage(src);
        }
        other->setItemHFlip(hFlip);
        other->setItemVFlip(vFlip);
        other->setContentHFlip(contentH);
        other->setContentVFlip(contentV);
        other->setSessionCrop(item->sessionHasCrop(), item->sessionCropRect());
    };
    for (ImageItem *other : peers) {
        syncOne(other);
    }

    // Durable snapshot: update the entry for this session image id.
    if (sessionId != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(sessionId)) {
            for (WorkspaceItemState &slot : m_workspace.savedItems()) {
                if (slot.sessionId != sessionId) {
                    continue;
                }
                slot.hasCrop = st->hasCrop;
                slot.cropRect = st->cropRect;
                slot.hFlip = hFlip;
                slot.vFlip = vFlip;
                slot.contentQuarterTurns = st->contentQuarterTurns;
                slot.contentHFlip = st->contentHFlip;
                slot.contentVFlip = st->contentVFlip;
                slot.orientation = 0.0;
                slot.sessionId = sessionId;
                slot.path = path;
            }
        }
    }

    emit statusChanged();
}













ImageItem *ImageView::findItemByPath(const QString &path) const
{
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            return item;
        }
    }
    return nullptr;
}

ImageItem *ImageView::findItemBySessionIndex(int sessionIndex) const
{
    if (sessionIndex < 0) {
        return nullptr;
    }
    for (ImageItem *item : m_items) {
        if (item->sessionIndex() == sessionIndex) {
            return item;
        }
    }
    return nullptr;
}

ImageItem *ImageView::findItemBySessionId(SessionImageId sessionId) const
{
    if (sessionId == kInvalidSessionImageId) {
        return nullptr;
    }
    for (ImageItem *item : m_items) {
        if (item->sessionId() == sessionId) {
            return item;
        }
    }
    for (ImageItem *item : m_workspace.stashedItems()) {
        if (item && item->sessionId() == sessionId) {
            return item;
        }
    }
    return nullptr;
}

void ImageView::removeWorkspaceSessionId(SessionImageId sessionId)
{
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    m_appearance.remove(sessionId);

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

    QStringList removedPaths;
    for (ImageItem *item : doomed) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        removedPaths.append(path);
        // Drop in-flight decodes so a late LoadAdd cannot create a tile or
        // call applyLayout after this session image is gone.
        m_pendingWorkspacePaths.remove(path);
        m_galleryDecodeScheduled.remove(path);
        m_galleryDecodeFailed.remove(path);
        m_pendingScenePos.remove(path);
        m_pendingSessionIndexByPath.remove(path);
        // destroyCanvasItem clears selection anchor / drag pointers and
        // removes from m_items and both stashes (safe if already only in one).
        destroyCanvasItem(item);
    }
    // Pending binds for this session id.
    for (int i = m_pendingSessionBinds.size() - 1; i >= 0; --i) {
        if (m_pendingSessionBinds.at(i).id == sessionId
            || removedPaths.contains(m_pendingSessionBinds.at(i).path)) {
            m_pendingSessionBinds.removeAt(i);
        }
    }

    for (int i = m_workspace.savedItems().size() - 1; i >= 0; --i) {
        if (m_workspace.savedItems().at(i).sessionId == sessionId) {
            m_workspace.savedItems().removeAt(i);
        }
    }

    // Keep path order aligned with remaining tiles (one path-order slot per
    // live item, including duplicate paths). A set-based prune left extra
    // path-order entries for the same path and later packs could look sparse.
    if (!removedPaths.isEmpty()) {
        QHash<QString, int> remaining;
        for (ImageItem *item : m_items) {
            if (item) {
                remaining[item->path()] += 1;
            }
        }
        QStringList pruned;
        pruned.reserve(m_items.size());
        for (const QString &path : m_pathOrder) {
            const auto it = remaining.find(path);
            if (it != remaining.end() && it.value() > 0) {
                pruned.append(path);
                it.value() -= 1;
            }
        }
        // Live tiles not represented in the prior order (should be rare).
        for (ImageItem *item : m_items) {
            if (!item) {
                continue;
            }
            const auto it = remaining.find(item->path());
            if (it != remaining.end() && it.value() > 0) {
                pruned.append(item->path());
                it.value() -= 1;
            }
        }
        m_pathOrder = pruned;
    }

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
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::setCurrentSessionId(SessionImageId id)
{
    m_currentSessionId = id;
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
    const QString path = item->path();
    m_pendingWorkspacePaths.remove(path);
    m_pendingScenePos.remove(path);
    m_pendingSessionIndexByPath.remove(path);
    m_galleryDecodeScheduled.remove(path);
    m_galleryDecodeFailed.remove(path);
    destroyCanvasItem(item);
    emit statusChanged();
    emit workspacePathsChanged();
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
        item->setSessionId(id);
        WorkspaceItemState slot = captureState(item);
        slot.sessionId = id;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        m_appearance.set(id, slot);
    }
}

int ImageView::workspacePathOccurrenceCount(const QString &path) const
{
    int n = 0;
    for (ImageItem *item : m_items) {
        if (item->path() == path) {
            ++n;
        }
    }
    return n;
}

void ImageView::removeWorkspacePathOccurrence(const QString &path, int occurrence)
{
    if (occurrence < 0) {
        return;
    }
    int found = 0;
    for (ImageItem *item : m_items) {
        if (item->path() != path) {
            continue;
        }
        if (found == occurrence) {
            takePendingWorkspacePath(path);
            m_pendingScenePos.remove(path);
            m_pendingSessionIndexByPath.remove(path);
            m_galleryDecodeScheduled.remove(path);
            m_galleryDecodeFailed.remove(path);
            destroyCanvasItem(item);
            emit statusChanged();
            emit workspacePathsChanged();
            return;
        }
        ++found;
    }
}

void ImageView::focusSessionPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    m_scene->clearSelection();
    item->setSelected(true);
    if (isGalleryMode()) {
        ensureVisible(item, 48, 48);
        // Keyboard focus: show filename in the HUD like mouse hover.
        if (m_gallery.hoverPath() != path) {
            m_gallery.setHoverPath(path);
            viewport()->update();
        }
    }
}

void ImageView::revealGalleryPath(const QString &path)
{
    if (path.isEmpty() || !isGalleryMode()) {
        return;
    }
    ImageItem *item = findItemByPath(path);
    if (!item) {
        return;
    }
    // Do not clearSelection — preserves Ctrl/Shift/rubber-band multi-select.
    ensureVisible(item, 48, 48);
    if (m_gallery.hoverPath() != path) {
        m_gallery.setHoverPath(path);
        viewport()->update();
    }
}



void ImageView::destroyCanvasItem(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Re-entrancy / double-destroy: after the first call the pointer is gone from
    // live and stash lists. A second call must not touch a deleted QGraphicsItem
    // (seen as SIGSEGV in QObject::blockSignals on a garbage scene pointer).
    const bool inLive = m_items.contains(item);
    const bool inGalleryStash = m_gallery.stashedItems().contains(item);
    const bool inWorkspaceStash = m_workspace.stashedItems().contains(item);
    if (!inLive && !inGalleryStash && !inWorkspaceStash) {
        return;
    }
    // AUDIT H8/H9: clear every view-owned pointer before delete so paint /
    // input cannot touch a dangling ImageItem (BSP crashes in scene paint).
    if (item == m_dragItem) {
        m_dragItem = nullptr;
    }
    if (item == m_rotateItem) {
        m_rotateItem = nullptr;
        m_rotating = false;
    }
    if (item == m_handleDragItem) {
        m_handleDragItem = nullptr;
    }
    if (item == m_gallery.selectionAnchor()) {
        m_gallery.setSelectionAnchor(nullptr);
    }
    // Group scale holds raw pointers — drop before delete or BSP paint UAF.
    if (m_groupScaleDrag || m_groupRotateDrag || !m_groupDragItems.isEmpty()) {
        m_groupScaleDrag = false;
        m_groupRotateDrag = false;
        m_groupHandle = -1;
        m_groupHoverHandle = -1;
        m_groupDragItems.clear();
        m_groupDragStartStates.clear();
    }
    // Also drop from gallery stash so discardStashedGallery cannot double-free.
    m_gallery.stashedItems().removeAll(item);
    m_workspace.stashedItems().removeAll(item);

    rememberItemState(item);
    m_items.removeAll(item);
    if (QGraphicsScene *sc = item->scene()) {
        // selectionChanged → statusChanged → paint must not run mid-teardown
        // (re-entrant paint was UAF in the BSP / item lists).
        const bool blocked = sc->blockSignals(true);
        item->setSelected(false);
        sc->removeItem(item);
        sc->blockSignals(blocked);
    } else {
        item->setSelected(false);
    }
    delete item;
    // TransformCommand stores raw ImageItem*; drop undo history that would
    // redo/undo against a deleted object — unless a session-level command is
    // intentionally removing canvas tiles and must stay on the stack.
    if (m_undoStack && !m_preserveUndoOnDestroy) {
        m_undoStack->clear();
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
}




qreal ImageView::pageGuidePxPerMm()
{
    // Workspace items use native image pixels as scene units. A 12MP photo is
    // ~4000px wide; at screen 96dpi an A4 sheet is only ~794px and looks tiny.
    // Use 300dpi so a page is roughly photo-scale (~2480×3508 for A4) while
    // still mapping 1:1 to physical paper on print/PDF.
    constexpr qreal kPageGuideDpi = 300.0;
    return kPageGuideDpi / 25.4;
}

void ImageView::setPageGuideVisible(bool on)
{
    if (m_pageGuideVisible == on) {
        return;
    }
    m_pageGuideVisible = on;
    if (m_pageGuideVisible && !m_pageGuideSize.isValid()) {
        const qreal pxPerMm = pageGuidePxPerMm();
        m_pageGuideSize = QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm);
    }
    if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    viewport()->update();
    emit statusChanged();
}

void ImageView::setPageGuideFromPrinter(const QPrinter &printer)
{
    // fullRect = physical paper (size + orientation). paintRect is only the
    // printable inset and can look unchanged when the user picks a different
    // stock size with similar aspect (or when margins dominate).
    const QPageLayout layout = printer.pageLayout();
    QRectF mm = layout.fullRect(QPageLayout::Millimeter);
    if (!mm.isValid() || mm.width() <= 1.0 || mm.height() <= 1.0) {
        const QSizeF sz = layout.pageSize().size(QPageSize::Millimeter);
        if (sz.width() > 1.0 && sz.height() > 1.0) {
            mm = QRectF(QPointF(0, 0), sz);
            if (layout.orientation() == QPageLayout::Landscape && mm.width() < mm.height()) {
                mm = QRectF(0, 0, mm.height(), mm.width());
            }
        } else {
            mm = QRectF(0, 0, 210.0, 297.0);
        }
    }
    const qreal pxPerMm = pageGuidePxPerMm();
    m_pageGuideSize = QSizeF(mm.width() * pxPerMm, mm.height() * pxPerMm);
    if (m_pageGuideVisible) {
        if (isWorkspaceMode()) {
            updateWorkspaceSceneRect();
        }
        viewport()->update();
        emit statusChanged();
    }
}


void ImageView::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid()) {
        return;
    }

    if (isWorkspaceMode() && m_pageGuideVisible && m_scene) {
        // Exact mapping: guide scene rect → page rect. KeepAspectRatio letterboxed
        // when full-sheet guide and margin-only pageRect differed, shifting items.
        const QRectF source = pageGuideSceneRect();
        m_scene->render(painter, pageRect, source, Qt::IgnoreAspectRatio);
        return;
    }

    if (isImageMode()) {
        ImageItem *item = primaryItem();
        if (!item) {
            item = targetItem();
        }
        if (item && item->hasDecodedPixels()) {
            const QImage &img = item->sourceImage();
            if (!img.isNull()) {
                QSizeF fitted(img.size());
                fitted.scale(pageRect.size(), Qt::KeepAspectRatio);
                const QRectF target(
                    pageRect.center().x() - fitted.width() / 2.0,
                    pageRect.center().y() - fitted.height() / 2.0,
                    fitted.width(), fitted.height());
                painter->save();
                painter->translate(target.center());
                painter->rotate(item->itemRotation());
                painter->scale(item->itemHFlip() ? -1.0 : 1.0,
                               item->itemVFlip() ? -1.0 : 1.0);
                painter->translate(-target.center());
                painter->drawImage(target, img);
                painter->restore();
                return;
            }
        }
    }

    if (m_scene) {
        QRectF source = m_scene->itemsBoundingRect();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        source.adjust(-4, -4, 4, 4);
        m_scene->render(painter, pageRect, source, Qt::KeepAspectRatio);
    }
}

QRectF ImageView::pageGuideSceneRect() const
{
    QSizeF sz = m_pageGuideSize;
    if (!sz.isValid() || sz.width() <= 0 || sz.height() <= 0) {
        const qreal pxPerMm = pageGuidePxPerMm();
        sz = QSizeF(210.0 * pxPerMm, 297.0 * pxPerMm);
    }
    return QRectF(-sz.width() / 2.0, -sz.height() / 2.0, sz.width(), sz.height());
}

























QSet<int> ImageView::workspaceSessionIndices() const
{
    QSet<int> out;
    for (ImageItem *item : m_items) {
        if (item->sessionIndex() >= 0) {
            out.insert(item->sessionIndex());
        }
    }
    return out;
}


QList<int> ImageView::selectedSessionIndices() const
{
    QList<int> out;
    for (ImageItem *item : m_items) {
        if (item->isSelected() && item->sessionIndex() >= 0) {
            out.append(item->sessionIndex());
        }
    }
    return out;
}






QList<ImageItem *> ImageView::transformTargets() const
{
    QList<ImageItem *> out;
    if (!m_scene) {
        return out;
    }
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *item = qgraphicsitem_cast<ImageItem *>(gi)) {
            out.append(item);
        }
    }
    if (!out.isEmpty()) {
        return out;
    }
    if (isImageMode() || m_items.size() == 1) {
        if (!m_items.isEmpty()) {
            out.append(m_items.first());
        }
    }
    return out;
}



QSizeF ImageView::nativeSize(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return QSizeF(item->pixmap().size());
}











