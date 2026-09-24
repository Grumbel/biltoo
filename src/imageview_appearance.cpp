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
    if (!item) {
        return;
    }
    // Stage 2: pose is Placement; content pixels stay on install paths only.
    item->applyPlacement(ItemComponents::placementFromState(state));
}

void ImageView::syncLiveContentMetaFromState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Applied fingerprint is presentation-local on the ImageItem only.
    // Never dual-write ItemWorld applied residual (outlived mode leave and
    // overrode sparse contentBake on Image underlay — CONTENTXFORM_AUTHORITY).
    const ContentXform::Value x = ContentXform::Value::fromState(state);
    item->setAppliedContentXform(x);
}

void ImageView::syncLiveColorFromState(ImageItem *item, const ColorAdjustments &grade,
                                       bool rebuildDisplay)
{
    if (!item) {
        return;
    }
    if (rebuildDisplay) {
        item->setColorAdjustments(grade);
    } else {
        item->setColorAdjustmentsRecord(grade);
    }
    const SessionImageId sid = item->sessionId();
    // Stage 2 residual: host-side live grade lag table when bound (paint keeps
    // item mirror). Distinct from durable ItemWorld Color.
    if (sid != kInvalidSessionImageId) {
        m_itemWorld.setLiveColorLag(sid, grade);
    }
    // When an applied ContentXform fingerprint is present, keep its colorAdjust
    // field coherent so paint / tile LOD prefer Value.colorAdjust.
    if (item->hasAppliedContentXform()) {
        ContentXform::Value x = item->tileContentXform();
        x.colorAdjust = grade;
        item->setAppliedContentXform(x);
    }
}

void ImageView::clearLiveContentMeta(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Identity: drop applied ContentXform fingerprint (item + ItemWorld when bound).
    item->clearAppliedContentXform();
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        m_itemWorld.clearAppliedContentXform(sid);
    }
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
    if (!item) {
        return;
    }
    // IDENTITY: one SessionImageId maps to one path. If a live or stashed tile
    // already holds this id on a different path, unbind it (peer-sync was
    // seeing sid=2 on both 001.jpg and 002.jpg).
    if (id != kInvalidSessionImageId && !item->path().isEmpty()) {
        auto scrub = [&](const QList<ImageItem *> &list) {
            for (ImageItem *other : list) {
                if (!other || other == item) {
                    continue;
                }
                if (other->sessionId() != id) {
                    continue;
                }
                if (other->path() == item->path()) {
                    continue;
                }
                qCritical("setItemSessionId: SessionImageId %lld path conflict "
                          "(%s vs %s) — unbinding other tile",
                          static_cast<long long>(id),
                          qPrintable(item->path()),
                          qPrintable(other->path()));
                other->setSessionId(kInvalidSessionImageId);
            }
        };
        scrub(m_items);
        scrub(m_workspace.stashedItems());
        scrub(m_gallery.stashedItems());
    }
    item->setSessionId(id);
    refreshSessionIndexCache(item);
    // Live lag is paint/slider residual. Durable Color is store authority.
    // Do not insert an identity lag row on every bind (pollutes hasLiveColorLag
    // and made freeze→setAppearance look like a grade commit).
    if (id != kInvalidSessionImageId) {
        switch (SessionAppearance::bindLiveColorLagAction(
            m_itemWorld.hasColor(id), item->colorAdjustments().isIdentity())) {
        case SessionAppearance::BindLagAction::FromDurableColor:
            m_itemWorld.setLiveColorLag(id, m_itemWorld.color(id).grade);
            break;
        case SessionAppearance::BindLagAction::FromItemGrade:
            m_itemWorld.setLiveColorLag(id, item->colorAdjustments());
            break;
        case SessionAppearance::BindLagAction::None:
            break;
        }
    }
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
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        // Pose-only; sparse Placement table (Stage 4b).
        m_itemWorld.setPlacement(sid, pl);
        return;
    }
    if (!item->path().isEmpty()) {
        WorkspaceItemState s;
        if (const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path())) {
            s = *prev;
        }
        ItemComponents::applyPlacementToState(s, pl);
        m_itemWorld.setPathState(item->path(), s);
    }
}

void ImageView::applyGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl)
{
    if (!item) {
        return;
    }
    item->applyPlacement(pl);
    persistGeometrySessionState(item, pl);
}


void ImageView::rememberItemState(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId editSid = resolveContentEditSessionId(item);
    // Pose-only for bound: freeze carries live color lag; setAppearance would
    // promote lag into durable Color (ECS_GUI_BYPASSES #5). Content commits
    // go through persistSessionAppearanceSlot / crop / bake paths.
    switch (SessionAppearance::rememberKind(isImageMode(), editSid, item->sessionId())) {
    case SessionAppearance::RememberKind::Skip:
        return;
    case SessionAppearance::RememberKind::WritePlacementOnly:
        m_itemWorld.setPlacement(item->sessionId(), placementFromItem(item));
        return;
    case SessionAppearance::RememberKind::WritePathFreeze: {
        WorkspaceItemState s = freezeItemAppearance(item);
        s.path = item->path();
        m_itemWorld.setPathState(item->path(), s);
        return;
    }
    }
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
    // Per-session-image appearance is a value copy keyed by stable id.
    // Image mode may bind the cursor id when the live item is not yet tagged.
    // Workspace/Gallery must not invent an id — that merges edits onto peers.
    const SessionImageId sid = resolveContentEditSessionId(item);
    WorkspaceItemState contentSlot;
    bool haveContentSlot = false;
    if (sid != kInvalidSessionImageId) {
        if (item->sessionId() == kInvalidSessionImageId) {
            setItemSessionId(item, sid);
        }
        WorkspaceItemState slot = freezeItemAppearance(item);
        slot.sessionId = sid;
        slot.sessionIndex = sessionListIndex(item);
        slot.path = item->path();
        // freeze may carry live color lag; durable Color is grade-commit only.
        slot = SessionAppearance::preferDurableColor(
            slot, m_itemWorld.hasColor(sid), m_itemWorld.color(sid).grade);
        // Full freeze replace into sparse tables (placement preserved when
        // identity — tip 2059). Color field is durable (above), not lag.
        m_itemWorld.setAppearance(sid, slot);
        slot = SessionAppearance::preferDurableColor(
            slot, m_itemWorld.hasColor(sid), m_itemWorld.color(sid).grade);
        contentSlot = slot;
        haveContentSlot = true;
    } else {
        // Unbound tile: still persist content-hash state for the file.
        contentSlot = freezeItemAppearance(item);
        haveContentSlot = true;
    }
    if (haveContentSlot) {
        // Durable local state (XDG_STATE_HOME/thumtoo): content-hash keyed.
        // Bound: orient/flip/grade only in XDG — crop lives in ItemWorld sparse by id.
        // Unbound: may include crop (legacy single-instance path edit).
        // Writing identity deletes the SQLite row; intentional clear goes
        // through clearContentAppearance (Reset / undo-to-identity).
        const bool writeCrop = SessionAppearance::shouldWriteCropToPathStore(
            sid != kInvalidSessionImageId, contentSlot.hasCrop,
            contentSlot.cropRect.isEmpty());
        ThumtooCache::StoredContentAppearance stored;
        if (SessionAppearance::fillStoredContentAppearance(
                &stored, contentSlot, writeCrop)) {
            ThumtooCache::saveContentAppearance(item->path(), stored);
        }
    }
    if (sid != kInvalidSessionImageId) {
        // Bound: do not last-write appearance onto the path map (duplicates
        // share a path). Placement remains in path book from Workspace
        // rememberItemState / snapshot only.
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            // Id-keyed only — path signals paint every filmstrip row with
            // the same file (IDENTITY.md).
            emit sessionAppearanceChanged(sid, item->path(), appearance);
            const bool hasCrop = m_itemWorld.hasCrop(sid)
                || itemAppliedContentXform(item).hasCrop;
            emit sessionCropApplied(sid, item->path(), appearance, hasCrop);
        }
    }
}

