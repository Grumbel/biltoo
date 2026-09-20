// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Item lookup, captureState, and related helpers restored after layout.cpp dissolve
// (7741d56) dropped their definitions without relocating them.

#include "imageview.h"
#include "imageitem.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "selectiongeometry.h"
#include "pageguidegeometry.h"

#include <QGraphicsItem>
#include <QVector>

WorkspaceItemState ImageView::captureState(const ImageItem *item) const
{
    WorkspaceItemState s;
    s.path = item->path();
    s.sessionId = item->sessionId();
    s.sessionIndex = item->sessionIndex(); // order cache only
    s.pos = item->pos();
    s.scale = item->itemScaleX();
    s.scaleY = item->itemScaleY();
    s.shear = item->itemShear();
    s.rotation = item->itemRotation(); // placement only
    s.orientation = 0.0;
    s.opacity = item->itemOpacity();
    s.z = item->stackZ();
    s.hFlip = item->itemHFlip();
    s.vFlip = item->itemVFlip();
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
        if (const WorkspaceItemState *app = appearance().get(sid)) {
            s.cropRotation = app->cropRotation;
            s.cropSourceSize = app->cropSourceSize;
            // Appearance store is sole content-orient authority for bound ids
            // (ContentXform ground truth). Never fall back to path map when
            // turns==0 — that resurrected stale 1..3 after a full 360° and
            // corrupted Gallery on the 4th rotate (commit → captureState →
            // m_appearance overwrite).
            s.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(app->contentQuarterTurns);
            if (!s.contentHFlip && app->contentHFlip) {
                s.contentHFlip = true;
            }
            if (!s.contentVFlip && app->contentVFlip) {
                s.contentVFlip = true;
            }
            if (!s.hasCrop && app->hasCrop) {
                s.hasCrop = app->hasCrop;
                s.cropRect = app->cropRect;
            }
        } else if (item->hasAppliedContentXform()) {
            // Store empty but live fingerprint exists (mid-edit).
            s.contentQuarterTurns = item->appliedContentXform().quarterTurns;
            s.contentHFlip = item->appliedContentXform().hFlip;
            s.contentVFlip = item->appliedContentXform().vFlip;
        }
        // Bound session image: path map is placement-only. Do not read content
        // turns/crop meta from m_itemStateBook.byPath.
    } else {
        // Unbound tile: path map may hold content orient.
        if (const WorkspaceItemState *prev = m_itemStateBook.get(item->path())) {
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
        if (const WorkspaceItemState *prev = m_itemStateBook.get(item->path())) {
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
    return appearance().value(id);
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
        if (const WorkspaceItemState *it = appearance().get(sid0)) {
            beforeSt.contentQuarterTurns =
                ContentXform::normalizeQuarterTurns(it->contentQuarterTurns);
            beforeSt.contentHFlip = it->contentHFlip;
            beforeSt.contentVFlip = it->contentVFlip;
        }
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
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *it = appearance().get(sid)) {
            cropMap = *it;
        }
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

