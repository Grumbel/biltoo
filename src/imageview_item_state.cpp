// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Item lookup, captureState, and related helpers restored after layout.cpp dissolve
// (7741d56) dropped their definitions without relocating them.

#include "imageview.h"
#include "session/sessiondocument.h"
#include "imageitem.h"
#include "content/contentxform.h"
#include "item/itemcomponents.h"
#include "session/sessionappearance.h"
#include "item/selectiongeometry.h"
#include "workspace/pageguidegeometry.h"

#include <QGraphicsItem>
#include <QVector>


ItemComponents::Placement ImageView::placementFromItem(const ImageItem *item)
{
    if (!item) {
        return {};
    }
    return item->placement();
}

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Interaction snapshot: durable content from ItemWorld sparse tables
    // (Stage 4b), then live pose / applied ContentXform / grade overlays.
    // Bound content starts from sessionAppearanceValue (single merge policy).
    const SessionImageId sid = resolveContentEditSessionId(item);
    const bool hasBoundDurable =
        sid != kInvalidSessionImageId && hasSessionAppearance(sid);
    WorkspaceItemState boundFallback;
    const WorkspaceItemState *boundPtr = nullptr;
    if (hasBoundDurable) {
        boundFallback = sessionAppearanceValue(sid);
        boundPtr = &boundFallback;
    }
    const WorkspaceItemState *pathState =
        (sid == kInvalidSessionImageId) ? m_itemWorld.getPathState(item->path())
                                        : nullptr;
    return SessionAppearance::assembleCaptureState(
        sid,
        item->path(),
        item->sessionId(),
        sessionListIndex(item),
        placementFromItem(item),
        hasBoundDurable,
        boundPtr,
        itemAppliedContentXform(item),
        itemHasAppliedContentXform(item),
        pathState,
        itemLiveColor(item));
}


WorkspaceItemState ImageView::freezeItemAppearance(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    const SessionImageId sid = resolveContentEditSessionId(item);
    // Durable path: sparse content is authority; overlay live pose/grade only.
    // Mid-edit applied ContentXform or unbound → full captureState.
    if (SessionAppearance::preferDurableFreeze(
            sid,
            itemHasAppliedContentXform(item),
            m_itemWorld.hasDurableAppearance(sid))) {
        return SessionAppearance::durableFreezeFromParts(
            sessionAppearanceValue(sid),
            placementFromItem(item),
            itemLiveColor(item),
            item->path(),
            sid,
            sessionListIndex(item));
    }
    return captureState(item);
}

WorkspaceItemState ImageView::sessionAppearanceValue(SessionImageId id) const
{
    // Sparse-prefer merge lives on ItemWorld::appearanceValue (single facade policy).
    return m_itemWorld.appearanceValue(id);
}

WorkspaceItemState ImageView::captureContentBakeBeforeState(ImageItem *item) const
{
    // Freeze: mid-edit applied ContentXform → captureState; else store + live.
    WorkspaceItemState beforeSt = freezeItemAppearance(item);
    beforeSt.sessionId = resolveContentEditSessionId(item);
    return beforeSt;
}

SessionImageId ImageView::resolveContentEditSessionId(const ImageItem *item) const
{
    if (!item) {
        return kInvalidSessionImageId;
    }
    return SessionAppearance::resolveEditSessionId(
        item->sessionId(), isImageMode(), m_session.identity().currentIdValue());
}

WorkspaceItemState ImageView::appearanceCropMapForEdit(ImageItem *item,
                                                       const WorkspaceItemState &fallback,
                                                       SessionImageId sid) const
{
    Q_UNUSED(item);
    if (sid != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(sid)) {
        // Sparse-prefer store read (sessionAppearanceValue choke point).
        return sessionAppearanceValue(sid);
    }
    return fallback;
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

ImageItem *ImageView::findPreferredItemForPath(const QString &path) const
{
    if (path.isEmpty() || !m_scene) {
        return nullptr;
    }
    // Selected tiles with this path win (duplicate open / crop must hit B not A).
    ImageItem *selectedMatch = nullptr;
    int selectedMatches = 0;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        auto *item = qgraphicsitem_cast<ImageItem *>(gi);
        if (!item || item->path() != path) {
            continue;
        }
        ++selectedMatches;
        if (!selectedMatch) {
            selectedMatch = item;
        }
    }
    // Sole live instance of this path.
    ImageItem *only = nullptr;
    int liveMatches = 0;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        ++liveMatches;
        only = item;
    }
    // Unique selected wins; else unique live; else ambiguous → null.
    return SelectionGeometry::preferUniquePathItem(
        selectedMatch, selectedMatches, only, liveMatches);
}

ImageItem *ImageView::findItemForPath(const QString &path) const
{
    if (ImageItem *pref = findPreferredItemForPath(path)) {
        return pref;
    }
    return findItemByPath(path);
}

int ImageView::sessionListIndex(const ImageItem *item) const
{
    if (!item) {
        return -1;
    }
    // Document order is authoritative when the item is bound.
    const bool bound = item->sessionId() != kInvalidSessionImageId;
    const int docIdx = (bound && m_sessionDoc)
        ? m_sessionDoc->indexOfId(item->sessionId())
        : -1;
    return SessionAppearance::preferSessionListIndex(
        docIdx, item->sessionIndex(), bound);
}


int ImageView::refreshSessionIndexCache(ImageItem *item)
{
    if (!item) {
        return -1;
    }
    // Document order when bound.
    const int listIdx = sessionListIndex(item);
    if (listIdx >= 0) {
        item->setSessionIndex(listIdx);
        return listIdx;
    }
    // Unbound: drop stale list-order cache. Pack-row hints must be restamped
    // by the caller via setSessionIndex after refresh (Gallery/Workspace).
    if (item->sessionId() == kInvalidSessionImageId) {
        item->setSessionIndex(-1);
    }
    return -1;
}

ImageItem *ImageView::findItemBySessionIndex(int sessionIndex) const
{
    if (sessionIndex < 0) {
        return nullptr;
    }
    // Document owns list slots when bound. A stale ImageItem::sessionIndex cache
    // must not return a different tile after reorder.
    if (m_sessionDoc && sessionIndex < m_sessionDoc->size()) {
        const SessionImageId id = m_sessionDoc->idAt(sessionIndex);
        if (id != kInvalidSessionImageId) {
            // Only the live tile with this id; nullptr if not on canvas yet.
            return findItemBySessionId(id);
        }
        // No id at this row — allow cache match only when path agrees.
        const QString path = m_sessionDoc->pathAt(sessionIndex);
        for (ImageItem *item : m_items) {
            if (!item || item->sessionIndex() != sessionIndex) {
                continue;
            }
            if (path.isEmpty() || item->path() == path) {
                return item;
            }
        }
        return nullptr;
    }
    // No document: list-order cache is the only signal.
    for (ImageItem *item : m_items) {
        if (item && item->sessionIndex() == sessionIndex) {
            return item;
        }
    }
    return nullptr;
}

ImageItem *ImageView::findItemBySessionId(SessionImageId sessionId) const
{
    // Live canvas only. Workspace (and Gallery) stashes hold parallel
    // presentations of the same SessionImageId — searching the Workspace stash
    // here made Gallery setWorkspacePaths/ensurePlaceholders treat a stashed
    // free-form tile as the Gallery cell and skip creating that session row
    // (individual images "disappeared" from Gallery after placing on Workspace).
    // Session remove uses collectItemsForSessionId (live + both stashes).
    if (sessionId == kInvalidSessionImageId) {
        return nullptr;
    }
    for (ImageItem *item : m_items) {
        if (item && item->sessionId() == sessionId) {
            return item;
        }
    }
    return nullptr;
}


ImageItem *ImageView::selectedOrFirstGalleryItem() const
{
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            return ii;
        }
    }
    return m_items.isEmpty() ? nullptr : m_items.first();
}

QRectF ImageView::selectionSceneBounds(const QList<ImageItem *> &items) const
{
    QVector<QRectF> rects;
    rects.reserve(items.size());
    for (ImageItem *item : items) {
        if (item) {
            rects.append(item->contentSceneRect());
        }
    }
    return SelectionGeometry::unionContentAabbs(rects);
}


void ImageView::flushAppliedContentToItemWorld()
{
    // Applied ContentXform is mid-edit *presentation* state. Sparse contentBake
    // / crop are the durable ground truth. Before mode leave: commit applied →
    // sparse, then clear ItemWorld applied residual so the next mode's underlay
    // materializes from contentBake only (not a stale dual-write fingerprint).
    for (ImageItem *item : m_items) {
        if (!item) {
            continue;
        }
        const SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId) {
            continue;
        }
        // Prefer item-local applied (this presentation); fall back to ItemWorld residual.
        ContentXform::Value applied;
        if (item->hasAppliedContentXform()) {
            applied = item->tileContentXform();
        } else if (m_itemWorld.hasAppliedContentXform(sid)) {
            applied = m_itemWorld.appliedContentXform(sid);
        } else {
            continue;
        }
        const WorkspaceItemState s = SessionAppearance::mergeAppliedIntoDurable(
            sessionAppearanceValue(sid), applied, sid, item->path());
        m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
        m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
        // Intentionally no setColor — applied.colorAdjust is not durable authority.
        // Drop live applied on the tile so stash/restore cannot treat mid-edit
        // fingerprint as parallel authority (ECS_GUI_BYPASSES #4 / #6).
        clearLiveContentMeta(item);
    }
    m_itemWorld.clearAllAppliedContentXforms();
}
