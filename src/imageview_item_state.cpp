// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Item lookup, captureState, and related helpers restored after layout.cpp dissolve
// (7741d56) dropped their definitions without relocating them.

#include "imageview.h"
#include "sessiondocument.h"
#include "imageitem.h"
#include "contentxform.h"
#include "itemcomponents.h"
#include "sessionappearance.h"
#include "selectiongeometry.h"
#include "pageguidegeometry.h"

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
    // Interaction snapshot: durable content from ItemWorld sparse tables
    // (Stage 4b), then live pose / applied ContentXform / grade overlays.
    // Bound content starts from sessionAppearanceValue (single merge policy).
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);

    WorkspaceItemState s;
    if (sid != kInvalidSessionImageId) {
        s = sessionAppearanceValue(sid);
    } else {
        // Unbound: live applied via tileContentXform; path map may hold
        // orient extras (quarter turns / crop source).
        const ContentXform::Value live = itemAppliedContentXform(item);
        s.hasCrop = live.hasCrop;
        s.cropRect = live.cropRect;
        s.contentHFlip = live.hFlip;
        s.contentVFlip = live.vFlip;
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(prev->contentQuarterTurns);
            s.cropRotation = prev->cropRotation;
            s.cropSourceSize = prev->cropSourceSize;
            if (prev->hasCrop && !s.hasCrop) {
                s.hasCrop = true;
                s.cropRect = prev->cropRect;
            }
        } else if (live.quarterTurns != 0) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(live.quarterTurns);
        }
    }

    s.path = item->path();
    s.sessionId = sid != kInvalidSessionImageId ? sid : item->sessionId();
    s.sessionIndex = sessionListIndex(item); // document order when bound
    // Live pose always wins (interaction may lead the Placement table).
    ItemComponents::applyPlacementToState(s, placementFromItem(item));

    if (sid != kInvalidSessionImageId) {
        // Applied ContentXform is mid-edit authority over sparse tables
        // (Gallery full-circle rotate must not resurrect turns). Without an
        // applied fingerprint, Stage 4b sparse assembly is sole content truth —
        // no legacy live-xform gap fill.
        if (itemHasAppliedContentXform(item)) {
            const ContentXform::Value live = itemAppliedContentXform(item);
            s.hasCrop = live.hasCrop;
            s.cropRect = live.cropRect;
            s.cropSourceSize = live.cropSourceSize;
            s.cropRotation = live.cropRotation;
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(live.quarterTurns);
            s.contentHFlip = live.hFlip;
            s.contentVFlip = live.vFlip;
        }
        // Live grade is interaction authority (slider may lead ItemWorld Color
        // until flushColorAdjustCommit).
        s.colorAdjust = itemLiveColor(item);
    } else {
        s.colorAdjust = itemLiveColor(item);
    }

    // Path-map list-index hint: unbound tiles only. Bound ids use
    // sessionListIndex / SessionDocument — do not adopt a stale path-book index.
    if (s.sessionIndex < 0 && item->sessionId() == kInvalidSessionImageId) {
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            if (prev->sessionIndex >= 0) {
                s.sessionIndex = prev->sessionIndex;
            }
        }
    }
    return s;
}

WorkspaceItemState ImageView::freezeItemAppearance(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId
        && !itemHasAppliedContentXform(item)
        && m_itemWorld.hasDurableAppearance(sid)) {
        WorkspaceItemState s = sessionAppearanceValue(sid);
        ItemComponents::applyPlacementToState(s, placementFromItem(item));
        s.colorAdjust = itemLiveColor(item);
        s.path = item->path();
        s.sessionId = sid;
        s.sessionIndex = sessionListIndex(item);
        return s;
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
    const SessionImageId sid0 = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    beforeSt.sessionId = sid0;
    return beforeSt;
}

SessionImageId ImageView::resolveContentEditSessionId(ImageItem *item) const
{
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    return sid;
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
    if (selectedMatches == 1) {
        return selectedMatch;
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
    if (liveMatches == 1) {
        return only;
    }
    // Ambiguous multi-match with no exclusive selection — caller falls back.
    return nullptr;
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
    if (m_sessionDoc && item->sessionId() != kInvalidSessionImageId) {
        const int i = m_sessionDoc->indexOfId(item->sessionId());
        if (i >= 0) {
            return i;
        }
    }
    return item->sessionIndex();
}

bool ImageView::sessionIdMatchesPath(SessionImageId id, const QString &path) const
{
    if (id == kInvalidSessionImageId) {
        return true;
    }
    if (!m_sessionDoc) {
        return true;
    }
    const int docIdx = m_sessionDoc->indexOfId(id);
    if (docIdx < 0) {
        return false;
    }
    if (path.isEmpty()) {
        return true;
    }
    return m_sessionDoc->paths().at(docIdx) == path;
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

qreal ImageView::pageGuidePxPerMm()
{
    // Workspace items use native image pixels as scene units. A 12MP photo is
    // ~4000px wide; at screen 96dpi an A4 sheet is only ~794px and looks tiny.
    // Use 300dpi so a page is roughly photo-scale (~2480×3508 for A4) while
    // still mapping 1:1 to physical paper on print/PDF.
    return PageGuideGeometry::pixelsPerMm();
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
        WorkspaceItemState s = sessionAppearanceValue(sid);
        // applyToState overwrites every field. Applied is orient/crop mid-edit
        // fingerprint (and paint may mirror live lag into applied.colorAdjust via
        // attachDisplaySample). Durable Color is only written by the grade commit
        // path (setTargetColorAdjustments) — never promote applied.colorAdjust.
        const ColorAdjustments durableColor = s.colorAdjust;
        const bool hadCrop = s.hasCrop;
        const QRect durableCropRect = s.cropRect;
        const QSize durableCropSource = s.cropSourceSize;
        const qreal durableCropRot = s.cropRotation;
        applied.applyToState(s);
        s.colorAdjust = durableColor;
        if (!applied.hasCrop && hadCrop) {
            s.hasCrop = true;
            s.cropRect = durableCropRect;
            s.cropSourceSize = durableCropSource;
            s.cropRotation = durableCropRot;
        }
        s.sessionId = sid;
        s.path = item->path();
        m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
        m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
        // Intentionally no setColor — applied.colorAdjust is not durable authority.
        // Drop live applied on the tile so stash/restore cannot treat mid-edit
        // fingerprint as parallel authority (ECS_GUI_BYPASSES #4 / #6).
        clearLiveContentMeta(item);
    }
    m_itemWorld.clearAllAppliedContentXforms();
}
