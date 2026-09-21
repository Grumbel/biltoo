// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance load/store/apply + Apply-undo (IDENTITY.md).
// Crop draft enter/leave/bake stays in imageview_crop.cpp.

#include "imageview.h"
#include <QString>
#include <QMetaObject>
#include <QThreadPool>
#include <QPointer>
#include "biltoo_thread.h"
#include <QTimer>
#include <QImage>
#include "viewtransform.h"
#include "itemcomponents.h"
#include "cropsession.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "thumtoocache.h"

#include "imageitem.h"
#include "sessionappearance.h"
#include "thumtoocache.h"
#include "imageloader.h"

const WorkspaceItemState *ImageView::resolveStoredAppearance(ImageItem *item,
                                                             WorkspaceItemState *fallback,
                                                             SessionImageId *sidOut)
{
    if (!item || !fallback) {
        return nullptr;
    }
    const SessionImageId sid = item->sessionId();
    if (sidOut) {
        *sidOut = sid;
    }
    if (sid != kInvalidSessionImageId) {
        // Seed orient/flip/grade from path XDG when the id slot is still empty
        // (restart / first bind). Crop is never seeded from path (IDENTITY).
        m_displayPipeline.seedSessionAppearanceFromState(sid, item->path());
        if (m_itemWorld.hasDurableAppearance(sid)) {
            // Always copy through sessionAppearanceValue so sparse Crop/Color/…
            // override lagging live xform (store-read authority).
            *fallback = sessionAppearanceValue(sid);
            return fallback;
        }
        // Bound with no durable content after seed = full frame.
        // NEVER fall back to the path map — that leaks crop/flip across
        // independent session images that share a file path.
        return nullptr;
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_itemWorld.getPathState(item->path())) {
        *fallback = *st;
        return fallback;
    }
    return nullptr;
}

void ImageView::applyStoredAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState fallback;
    SessionImageId sid = kInvalidSessionImageId;
    const WorkspaceItemState *app = resolveStoredAppearance(item, &fallback, &sid);
    if (!app) {
        return;
    }
    const bool needsFullSource = app->hasCrop || app->contentHFlip || app->contentVFlip
        || app->contentQuarterTurns != 0;
    if (needsFullSource) {
        const QImage full = m_displayPipeline.fullRasterForEdit(item->path());
        if (!full.isNull()) {
            m_displayPipeline.installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                 sid);
            return;
        }
    }
    rematerializeItemContent(item, *app);
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
    if (sid != kInvalidSessionImageId && m_itemWorld.hasLiveColorLag(sid)) {
        return m_itemWorld.liveColorLag(sid);
    }
    return item->colorAdjustments();
}

void ImageView::clearItemDecodedPixels(ImageItem *item)
{
    if (!item) {
        return;
    }
    item->clearDecodedPixels();
}

void ImageView::setItemIntrinsicSize(ImageItem *item, const QSize &size)
{
    if (!item) {
        return;
    }
    item->setIntrinsicSize(size);
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
    // and made freeze→setAppearance look like a grade commit). Prefer durable
    // Color when present; only stamp item grade when non-identity.
    if (id != kInvalidSessionImageId) {
        if (m_itemWorld.hasColor(id)) {
            m_itemWorld.setLiveColorLag(id, m_itemWorld.color(id).grade);
        } else if (!item->colorAdjustments().isIdentity()) {
            m_itemWorld.setLiveColorLag(id, item->colorAdjustments());
        }
        // else: leave lag table unchanged (no identity row)
    }
}

void ImageView::setItemSessionIndex(ImageItem *item, int index)
{
    if (!item) {
        return;
    }
    item->setSessionIndex(index);
}

void ImageView::setItemPreviewImage(ImageItem *item, const QImage &preview)
{
    if (!item) {
        return;
    }
    item->setPreviewImage(preview);
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
    const SessionImageId sid =
        item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);

    // Image mode must not overwrite Workspace placement (pos / scale / free tilt).
    // Bound session images: appearance lives in ItemWorld sparse tables (Stage 4b).
    if (isImageMode()) {
        if (sid != kInvalidSessionImageId) {
            // Leave path-map placement untouched; do not last-write crop/flip by path.
            return;
        }
        // Unbound legacy tile: path map is the only store (freeze → captureState).
        WorkspaceItemState s = freezeItemAppearance(item);
        s.path = item->path();
        m_itemWorld.setPathState(item->path(), s);
        return;
    }
    // Workspace / Gallery: path map is legacy placement for *unbound* tiles only.
    // Bound session images: placement + content live in ItemWorld sparse tables (by id).
    // Never write pose by path — duplicates would steal each other's layout.
    // Pose-only for bound: freeze carries live color lag; setAppearance would
    // promote lag into durable Color (2194 / ECS_GUI_BYPASSES #5). Content commits
    // go through persistSessionAppearanceSlot / crop / bake paths.
    if (item->sessionId() != kInvalidSessionImageId) {
        m_itemWorld.setPlacement(item->sessionId(), placementFromItem(item));
        return;
    }
    m_itemWorld.setPathState(item->path(), freezeItemAppearance(item));
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
    QImage img = item->displayImage();
    if (img.isNull()) {
        return {};
    }
    // Placement display flips (should be empty after content bake into pixels).
    {
        const ItemComponents::Placement pl = item->placement();
        if (pl.hFlip || pl.vFlip) {
            Qt::Orientations axes;
            if (pl.hFlip) {
                axes |= Qt::Horizontal;
            }
            if (pl.vFlip) {
                axes |= Qt::Vertical;
            }
            img = img.flipped(axes);
        }
    }
    // Live grade only when display is still unbaked host (no applied xform).
    // Re-applying on a materialize bake double-grades the filmstrip override.
    if (!itemHasAppliedContentXform(item)) {
        const ColorAdjustments adj = itemLiveColor(item);
        if (!adj.isIdentity()) {
            img = applyColorAdjustments(img, adj);
        }
    }
    return img;
}


QImage ImageView::imageWithSessionAppearance(const QImage &src, SessionImageId sid,
                                             const QString &path) const
{
    if (src.isNull()) {
        return {};
    }
    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState fallback;
    if (sid != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(sid)) {
        fallback = sessionAppearanceValue(sid);
        app = &fallback;
    }
    // Unbound only: path map + path XDG. Bound content is ItemWorld sparse only
    // (matches Image underlay / contentLayoutSize — no path XDG for bound ids).
    if ((!app || !SessionAppearance::hasContentAppearance(*app))
        && sid == kInvalidSessionImageId && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            fallback = *st;
            app = &fallback;
        }
        if ((!app || !SessionAppearance::hasContentAppearance(*app))) {
            ThumtooCache::StoredContentAppearance stored;
            if (ThumtooCache::loadContentAppearance(path, &stored)
                && (stored.contentHFlip || stored.contentVFlip
                    || stored.contentQuarterTurns != 0 || stored.hasCrop)) {
                fallback = {};
                fallback.path = path;
                fallback.contentHFlip = stored.contentHFlip;
                fallback.contentVFlip = stored.contentVFlip;
                fallback.contentQuarterTurns = stored.contentQuarterTurns;
                if (stored.hasCrop) {
                    fallback.hasCrop = true;
                    fallback.cropRect = stored.cropRect;
                    fallback.cropSourceSize = stored.cropSourceSize;
                    fallback.cropRotation = stored.cropRotation;
                }
                app = &fallback;
            }
        }
    }
    // Prefer sparse Color grade for filmstrip / soft paint. Grade-only sparse
    // presence still materializes when only color (or other single component) is set.
    WorkspaceItemState paint;
    if (app) {
        paint = *app;
    } else if (sid == kInvalidSessionImageId || !m_itemWorld.hasColor(sid)) {
        return src;
    }
    if (sid != kInvalidSessionImageId) {
        paint.colorAdjust = m_itemWorld.color(sid).grade;
        paint.sessionId = sid;
    }
    if (!SessionAppearance::hasContentAppearance(paint) && paint.colorAdjust.isIdentity()) {
        return src;
    }
    // Single pipeline — SoftPreview scales crop into soft pixel space
    // (SessionAppearance::materializeDisplay contract). Never strip crop.
    const QImage out = SessionAppearance::materializeDisplay(
        src, paint, SessionAppearance::PixelKind::SoftPreview);
    return out.isNull() ? src : out;
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
    // Bound session images: path XDG keeps orient/flip/grade as a file-level
    // hint; crop stays SessionImageId-only (duplicates share a path — IDENTITY).
    const bool bound = item && item->sessionId() != kInvalidSessionImageId;
    const bool writeCrop = !bound && s.hasCrop && !s.cropRect.isEmpty();
    const bool hasGrade = !s.colorAdjust.isIdentity();
    const bool contentful =
        s.contentHFlip || s.contentVFlip
        || s.contentQuarterTurns != 0
        || writeCrop
        || hasGrade;
    if (contentful) {
        ThumtooCache::StoredContentAppearance stored;
        stored.contentHFlip = s.contentHFlip;
        stored.contentVFlip = s.contentVFlip;
        stored.contentQuarterTurns = s.contentQuarterTurns;
        stored.hasCrop = writeCrop;
        if (writeCrop) {
            stored.cropRect = s.cropRect;
            stored.cropSourceSize = s.cropSourceSize;
            stored.cropRotation = s.cropRotation;
        }
        if (hasGrade) {
            stored.hasGrade = true;
            stored.gradeBrightness = s.colorAdjust.brightness;
            stored.gradeContrast = s.colorAdjust.contrast;
            stored.gradeSaturation = s.colorAdjust.saturation;
            stored.gradeHue = s.colorAdjust.hue;
            // Durable gamma is percent (100 = 1.0).
            stored.gradeGamma = ColorAdjustments::gammaToPercent(s.colorAdjust.gamma);
            stored.gradeInvert = s.colorAdjust.invert;
        }
        ThumtooCache::saveContentAppearance(item->path(), stored);
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE")) {
            qWarning().noquote()
                << QStringLiteral("[appearance] %1 save path=%2 h=%3 v=%4 turns=%5")
                       .arg(QLatin1String(debugTag))
                       .arg(item->path())
                       .arg(s.contentHFlip)
                       .arg(s.contentVFlip)
                       .arg(s.contentQuarterTurns);
        }
    } else {
        ThumtooCache::clearContentAppearance(item->path());
        if (qEnvironmentVariableIsSet("BILTOO_DEBUG_APPEARANCE")) {
            qWarning().noquote()
                << QStringLiteral("[appearance] %1 clear (identity) path=%2")
                       .arg(QLatin1String(debugTag))
                       .arg(item->path());
        }
    }
}











void ImageView::persistSessionAppearanceSlot(ImageItem *item)
{
    // Per-session-image appearance is a value copy keyed by stable id.
    SessionImageId sid = item->sessionId();
    // Image mode may bind the cursor id when the live item is not yet tagged.
    // Workspace/Gallery must not invent an id — that merges edits onto peers.
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
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
        // freeze may carry live color lag in colorAdjust. Durable Color is
        // written only by setTargetColorAdjustments — never promote lag here.
        if (m_itemWorld.hasColor(sid)) {
            slot.colorAdjust = m_itemWorld.color(sid).grade;
        }
        // Full freeze replace into sparse tables (placement preserved when
        // identity — tip 2059). Color field is durable (above), not lag.
        m_itemWorld.setAppearance(sid, slot);
        slot.colorAdjust = m_itemWorld.color(sid).grade;
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
        const bool writeCrop = (sid == kInvalidSessionImageId)
            && contentSlot.hasCrop && !contentSlot.cropRect.isEmpty();
        const bool hasGrade = !contentSlot.colorAdjust.isIdentity();
        const bool contentful =
            contentSlot.contentHFlip || contentSlot.contentVFlip
            || contentSlot.contentQuarterTurns != 0
            || writeCrop
            || hasGrade;
        if (contentful) {
            ThumtooCache::StoredContentAppearance stored;
            stored.contentHFlip = contentSlot.contentHFlip;
            stored.contentVFlip = contentSlot.contentVFlip;
            stored.contentQuarterTurns = contentSlot.contentQuarterTurns;
            stored.hasCrop = writeCrop;
            if (writeCrop) {
                stored.cropRect = contentSlot.cropRect;
                stored.cropSourceSize = contentSlot.cropSourceSize;
                stored.cropRotation = contentSlot.cropRotation;
            }
            if (hasGrade) {
                stored.hasGrade = true;
                stored.gradeBrightness = contentSlot.colorAdjust.brightness;
                stored.gradeContrast = contentSlot.colorAdjust.contrast;
                stored.gradeSaturation = contentSlot.colorAdjust.saturation;
                stored.gradeHue = contentSlot.colorAdjust.hue;
                stored.gradeGamma =
                    ColorAdjustments::gammaToPercent(contentSlot.colorAdjust.gamma);
                stored.gradeInvert = contentSlot.colorAdjust.invert;
            }
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

