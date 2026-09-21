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
    WorkspaceItemState s;
    s.path = item->path();
    s.sessionId = item->sessionId();
    s.sessionIndex = sessionListIndex(item); // document order when bound
    // Stage 2: pose via Placement helper (single place that reads item pose).
    ItemComponents::applyPlacementToState(s, placementFromItem(item));
    s.orientation = 0.0;
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        // Phase 7: when applied ContentXform is present, it is mid-edit authority
        // over sparse tables (Gallery full-circle rotate must not resurrect turns).
        // Otherwise sparse tables, then live applied via tileContentXform.
        if (item->hasAppliedContentXform()) {
            const ContentXform::Value live = item->tileContentXform();
            s.hasCrop = live.hasCrop;
            s.cropRect = live.cropRect;
            s.cropSourceSize = live.cropSourceSize;
            s.cropRotation = live.cropRotation;
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(live.quarterTurns);
            s.contentHFlip = live.hFlip;
            s.contentVFlip = live.vFlip;
        } else {
            if (m_itemWorld.hasCrop(sid)) {
                const ItemComponents::Crop crop = m_itemWorld.crop(sid);
                s.hasCrop = !crop.isEmpty();
                s.cropRect = crop.rect;
                s.cropRotation = crop.rotation;
                s.cropSourceSize = crop.sourceSize;
            } else {
                const ContentXform::Value live = item->tileContentXform();
                s.hasCrop = live.hasCrop;
                s.cropRect = live.cropRect;
            }
            if (m_itemWorld.hasContentBake(sid)) {
                const ItemComponents::ContentBake bake = m_itemWorld.contentBake(sid);
                s.contentQuarterTurns =
                    ContentXform::normalizeQuarterTurns(bake.quarterTurns);
                s.contentHFlip = bake.hFlip;
                s.contentVFlip = bake.vFlip;
            } else {
                const ContentXform::Value live = item->tileContentXform();
                s.contentQuarterTurns =
                    ContentXform::normalizeQuarterTurns(live.quarterTurns);
                s.contentHFlip = live.hFlip;
                s.contentVFlip = live.vFlip;
            }
        }
        // Live grade is interaction authority (slider may lead ItemWorld Color
        // until flushColorAdjustCommit). Same idea as pose-from-item.
        s.colorAdjust = item->colorAdjustments();
        if (m_itemWorld.hasAttention(sid)) {
            s.attentionPoints = m_itemWorld.attention(sid).points;
            s.syncAttentionPrimary();
        }
        // Bound session image: path map is placement-only.
    } else {
        // Unbound: live applied via tileContentXform; path map may hold
        // orient extras (quarter turns / crop source).
        const ContentXform::Value live = item->tileContentXform();
        s.hasCrop = live.hasCrop;
        s.cropRect = live.cropRect;
        s.contentHFlip = live.hFlip;
        s.contentVFlip = live.vFlip;
        s.colorAdjust = item->colorAdjustments();
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(prev->contentQuarterTurns);
            s.cropRotation = prev->cropRotation;
            s.cropSourceSize = prev->cropSourceSize;
            if (s.sessionIndex < 0 && prev->sessionIndex >= 0) {
                s.sessionIndex = prev->sessionIndex;
            }
        } else if (live.quarterTurns != 0) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(live.quarterTurns);
        }
    }
    // Placement path-map hint for session index only (bound or unbound).
    if (s.sessionIndex < 0) {
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            if (prev->sessionIndex >= 0) {
                s.sessionIndex = prev->sessionIndex;
            }
        }
    }
    return s;
}

WorkspaceItemState ImageView::sessionAppearanceValue(SessionImageId id) const
{
    // Sparse-prefer merge lives on ItemWorld::appearanceValue (single facade policy).
    return m_itemWorld.appearanceValue(id);
}

WorkspaceItemState ImageView::captureContentBakeBeforeState(ImageItem *item) const
{
    // captureState already prefers applied ContentXform when present.
    WorkspaceItemState beforeSt = captureState(item);
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
    if (sid != kInvalidSessionImageId && m_itemWorld.hasAppearance(sid)) {
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

