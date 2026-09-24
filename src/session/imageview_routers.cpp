// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers / host gather co-located with session/ appearance ownership.

#include "imageview.h"
#include "imageitem.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"
#include "item/itemcomponents.h"
#include "content/contentxform.h"
#include "color/coloradjust.h"

// --- from imageview_appearance.cpp ---
const WorkspaceItemState *ImageView::resolveStoredAppearance(ImageItem *item,
                                                             WorkspaceItemState *fallback,
                                                             SessionImageId *sidOut)
{
    return m_displayPipeline->resolveStoredAppearance(item, fallback, sidOut);
}

void ImageView::applyStoredAppearance(ImageItem *item)
{
    m_displayPipeline->applyStoredAppearance(item);
}


bool ImageView::loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const
{
    if (!st || sid == kInvalidSessionImageId) {
        return false;
    }
    // Sparse rows count (Stage 4b — hasDurableAppearance).
    if (!m_itemWorld.hasDurableAppearance(sid)) {
        return false;
    }
    *st = sessionAppearanceValue(sid);
    return true;
}




// --- from imageview_layout.cpp (appearance) ---

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    m_image.applyState(item, state);
}


void ImageView::syncLiveContentMetaFromState(ImageItem *item, const WorkspaceItemState &state)
{
    m_image.syncLiveContentMetaFromState(item, state);
}


void ImageView::syncLiveColorFromState(ImageItem *item, const ColorAdjustments &grade,
                                       bool rebuildDisplay)
{
    m_image.syncLiveColorFromState(item, grade, rebuildDisplay);
}


void ImageView::clearLiveContentMeta(ImageItem *item)
{
    m_image.clearLiveContentMeta(item);
}


bool ImageView::itemHasAppliedContentXform(const ImageItem *item) const
{
    // Applied is presentation-local on the ImageItem. ItemWorld applied residual
    // must not count — it outlived mode leave and poisoned Image underlay want.
    return item && item->hasAppliedContentXform();
}

ContentXform::Value ImageView::itemAppliedContentXform(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    if (item->hasAppliedContentXform()) {
        return item->tileContentXform();
    }
    return {};
}


ColorAdjustments ImageView::itemLiveColor(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Prefer ItemWorld runtime lag when bound (host-side scratch); item mirror
    // for paint / unbound. Durable grade remains ItemWorld Color.
    const SessionImageId sid = item->sessionId();
    const bool hasLag = sid != kInvalidSessionImageId
        && m_itemWorld.hasLiveColorLag(sid);
    return SessionAppearance::preferLiveColor(
        hasLag,
        hasLag ? m_itemWorld.liveColorLag(sid) : ColorAdjustments{},
        item->colorAdjustments());
}

void ImageView::setItemSessionId(ImageItem *item, SessionImageId id)
{
    m_image.setItemSessionId(item, id);
}


void ImageView::setItemSessionIndex(ImageItem *item, int index)
{
    if (!item) {
        return;
    }
    item->setSessionIndex(index);
}

void ImageView::persistGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl)
{
    m_image.persistGeometrySessionState(item, pl);
}


void ImageView::applyGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl)
{
    if (!item) {
        return;
    }
    item->applyPlacement(pl);
    m_image.persistGeometrySessionState(item, pl);
}



void ImageView::rememberItemState(ImageItem *item)
{
    m_image.rememberItemState(item);
}



QImage ImageView::sessionAppearanceImage(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Content 90°/flip/crop/grade are baked into source *or* soft-preview pixels
    // once an applied ContentXform is set. Gallery soft tiles often have only
    // m_preview — using sourceImage alone left the filmstrip on the unflipped
    // ladder after Gallery flip/rotate. Placement rotation is not included.
    const ItemComponents::Placement pl = item->placement();
    return SessionAppearance::applyLiveDisplayOverlays(
        item->displayImage(), pl.hFlip, pl.vFlip,
        itemHasAppliedContentXform(item), itemLiveColor(item));
}


QImage ImageView::imageWithSessionAppearance(const QImage &src, SessionImageId sid,
                                             const QString &path) const
{
    const WorkspaceItemState *boundApp = nullptr;
    WorkspaceItemState boundFallback;
    if (sid != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(sid)) {
        boundFallback = sessionAppearanceValue(sid);
        boundApp = &boundFallback;
    }
    const WorkspaceItemState *pathState = nullptr;
    if (sid == kInvalidSessionImageId && !path.isEmpty()) {
        pathState = m_itemWorld.getPathState(path);
    }
    const bool hasSparseColor =
        sid != kInvalidSessionImageId && m_itemWorld.hasColor(sid);
    return SessionAppearance::softImageWithAppearanceSources(
        src, sid, path, boundApp, pathState, hasSparseColor,
        hasSparseColor ? m_itemWorld.color(sid).grade : ColorAdjustments{});
}



bool ImageView::hasSessionAppearance(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(id);
}


void ImageView::setSessionAppearance(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    // ItemWorld::setAppearance writes sparse Crop/ContentBake/Color/… tables.
    m_itemWorld.setAppearance(id, state);
}


void ImageView::persistDurableContentAppearance(ImageItem *item, const WorkspaceItemState &s,
                                                const char *debugTag)
{
    if (!item) {
        return;
    }
    SessionAppearance::persistPathContentAppearance(
        item->path(),
        item->sessionId() != kInvalidSessionImageId,
        s,
        debugTag);
}












void ImageView::persistSessionAppearanceSlot(ImageItem *item)
{
    m_image.persistSessionAppearanceSlot(item);
}



// --- from imageview_item_state.cpp ---
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
    m_image.flushAppliedContentToItemWorld();
}


