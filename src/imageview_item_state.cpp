// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Item lookup, captureState, and related helpers restored after layout.cpp dissolve
// (7741d56) dropped their definitions without relocating them.

#include "imageview.h"
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
    s.sessionIndex = item->sessionIndex(); // order cache only
    // Stage 2: pose via Placement helper (single place that reads item pose).
    ItemComponents::applyPlacementToState(s, placementFromItem(item));
    s.orientation = 0.0;
    // Live item is authoritative for per-instance crop rect + content flips.
    // cropRotation / cropSourceSize are not stored on ImageItem — load them
    // from the session-image appearance store (or path map for unbound).
    s.hasCrop = item->sessionHasCrop();
    s.cropRect = item->sessionCropRect();
    s.contentHFlip = item->contentHFlip();
    s.contentVFlip = item->contentVFlip();
    s.colorAdjust = item->colorAdjustments();
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        // Phase 7: prefer sparse components over fat DTO field reads.
        // ContentBake is sole orient authority for bound ids (never path map).
        if (m_itemWorld.hasAppearance(sid) || m_itemWorld.hasContentBake(sid)
            || m_itemWorld.hasCrop(sid)) {
            const ItemComponents::ContentBake bake = m_itemWorld.contentBake(sid);
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(bake.quarterTurns);
            if (!s.contentHFlip && bake.hFlip) {
                s.contentHFlip = true;
            }
            if (!s.contentVFlip && bake.vFlip) {
                s.contentVFlip = true;
            }
            const ItemComponents::Crop crop = m_itemWorld.crop(sid);
            if (!crop.isEmpty()) {
                s.cropRotation = crop.rotation;
                s.cropSourceSize = crop.sourceSize;
                if (!s.hasCrop) {
                    s.hasCrop = true;
                    s.cropRect = crop.rect;
                }
            }
        } else if (item->hasAppliedContentXform()) {
            // Store empty but live fingerprint exists (mid-edit).
            s.contentQuarterTurns = item->appliedContentXform().quarterTurns;
            s.contentHFlip = item->appliedContentXform().hFlip;
            s.contentVFlip = item->appliedContentXform().vFlip;
        }
        // Bound session image: path map is placement-only.
    } else {
        // Unbound tile: path map may hold content orient.
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(prev->contentQuarterTurns);
            s.cropRotation = prev->cropRotation;
            s.cropSourceSize = prev->cropSourceSize;
            if (s.sessionIndex < 0 && prev->sessionIndex >= 0) {
                s.sessionIndex = prev->sessionIndex;
            }
        } else if (item->hasAppliedContentXform()) {
            s.contentQuarterTurns = item->appliedContentXform().quarterTurns;
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
    if (id == kInvalidSessionImageId) {
        return {};
    }
    return m_itemWorld.appearanceValue(id);
}

WorkspaceItemState ImageView::captureContentBakeBeforeState(ImageItem *item) const
{
    // ContentXform ground truth: applied fingerprint > appearance store >
    // captureState placement. Never path-map turns for bound ids.
    WorkspaceItemState beforeSt = captureState(item);
    beforeSt.hasCrop = item->sessionHasCrop();
    beforeSt.cropRect = item->sessionCropRect();
    beforeSt.contentHFlip = item->contentHFlip();
    beforeSt.contentVFlip = item->contentVFlip();
    const SessionImageId sid0 = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    beforeSt.sessionId = sid0;
    if (item->hasAppliedContentXform()) {
        const ContentXform::Value x = item->appliedContentXform();
        beforeSt.contentQuarterTurns = x.quarterTurns;
        beforeSt.contentHFlip = x.hFlip;
        beforeSt.contentVFlip = x.vFlip;
        beforeSt.hasCrop = x.hasCrop;
        beforeSt.cropRect = x.cropRect;
        beforeSt.cropSourceSize = x.cropSourceSize;
        beforeSt.cropRotation = x.cropRotation;
        return beforeSt;
    }
    if (sid0 != kInvalidSessionImageId) {
        const ItemComponents::ContentBake bake = m_itemWorld.contentBake(sid0);
        beforeSt.contentQuarterTurns =
            ContentXform::normalizeQuarterTurns(bake.quarterTurns);
        beforeSt.contentHFlip = bake.hFlip;
        beforeSt.contentVFlip = bake.vFlip;
    }
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
    WorkspaceItemState cropMap = fallback;
    if (sid != kInvalidSessionImageId && m_itemWorld.hasAppearance(sid)) {
        cropMap = m_itemWorld.appearanceValue(sid);
        // Overlay sparse crop when present (presence API).
        const ItemComponents::Crop crop = m_itemWorld.crop(sid);
        if (!crop.isEmpty()) {
            ItemComponents::applyCropToState(cropMap, crop);
        }
        const ItemComponents::ContentBake bake = m_itemWorld.contentBake(sid);
        ItemComponents::applyContentBakeToState(cropMap, bake);
    }
    return cropMap;
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

