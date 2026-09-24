// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance load/store/apply + Apply-undo (IDENTITY.md).
// Crop draft enter/leave/bake stays in imageview_crop.cpp.

#include "imageview.h"
#include <QString>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include "util/biltoo_thread.h"
#include <QTimer>
#include <QImage>
#include "view/viewtransform.h"
#include "item/itemcomponents.h"
#include "crop/cropsession.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"

#include "imageitem.h"
#include "session/sessionappearance.h"
#include "host/thumtoocache.h"
#include "host/imageloader.h"
#include "display/displayquality.h"

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


