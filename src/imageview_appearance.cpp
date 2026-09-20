// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance load/store/apply + Apply-undo (IDENTITY.md).
// Crop draft enter/leave/bake stays in imageview_crop.cpp.

#include "imageview.h"
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
        seedSessionAppearanceFromState(sid, item->path());
        if (const WorkspaceItemState *it = m_itemWorld.getAppearance(sid)) {
            return it;
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
        const QImage full = fullRasterForEdit(item->path());
        if (!full.isNull()) {
            installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                 sid);
            return;
        }
    }
    rematerializeItemContent(item, *app);
}

void ImageView::applyContentAppearanceAfterDecode(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState fallback;
    const WorkspaceItemState *app = resolveStoredAppearance(item, &fallback, nullptr);
    if (!app || !SessionAppearance::hasContentAppearance(*app)) {
        return;
    }
    // Caller just installed full on-disk pixels; do not load again.
    rematerializeItemContent(item, *app);
}

bool ImageView::loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const
{
    if (!st || sid == kInvalidSessionImageId) {
        return false;
    }
    if (const WorkspaceItemState *it = m_itemWorld.getAppearance(sid)) {
        *st = *it;
        return true;
    }
    return false;
}




// --- from imageview_layout.cpp (appearance) ---

void ImageView::applyPlacement(ImageItem *item, const ItemComponents::Placement &pl)
{
    if (!item) {
        return;
    }
    item->setPos(pl.pos);
    item->setItemScale(pl.scale, pl.scaleY > 0.0 ? pl.scaleY : pl.scale);
    item->setItemShear(pl.shear);
    item->setItemRotation(pl.rotation);
    item->setItemOpacity(pl.opacity);
    item->setStackZ(pl.z);
    item->setItemHFlip(pl.hFlip);
    item->setItemVFlip(pl.vFlip);
}

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Stage 2: pose is Placement; content pixels stay on install paths only.
    applyPlacement(item, ItemComponents::placementFromState(state));
}

void ImageView::persistGeometrySessionState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : state.sessionId;
    if (sid != kInvalidSessionImageId) {
        m_itemWorld.setPlacement(sid, ItemComponents::placementFromState(state));
        m_itemWorld.setAppearance(sid, state);
        return;
    }
    if (!item->path().isEmpty()) {
        m_itemWorld.setPathState(item->path(), state);
    }
}

void ImageView::applyGeometrySessionState(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    applyState(item, state);
    persistGeometrySessionState(item, state);
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
    // Bound session images: appearance lives only in m_appearance (Phase 2).
    if (isImageMode()) {
        if (sid != kInvalidSessionImageId) {
            // Leave path-map placement untouched; do not last-write crop/flip by path.
            return;
        }
        // Unbound legacy tile: path map is the only store.
        WorkspaceItemState s;
        const WorkspaceItemState *prev = m_itemWorld.getPathState(item->path());
        if (prev) {
            s = *prev;
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
        s.colorAdjust = item->colorAdjustments();
        if (prev) {
            s.contentQuarterTurns = prev->contentQuarterTurns;
            s.cropRotation = prev->cropRotation;
            s.cropSourceSize = prev->cropSourceSize;
        }
        m_itemWorld.setPathState(item->path(), s);
        return;
    }
    // Workspace / Gallery: path map is legacy placement for *unbound* tiles only.
    // Bound session images: placement + content live in m_appearance (by id).
    // Never write pose by path — duplicates would steal each other's layout.
    if (item->sessionId() != kInvalidSessionImageId) {
        WorkspaceItemState slot = captureState(item);
        slot.sessionId = item->sessionId();
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        m_itemWorld.setPlacement(item->sessionId(),
                                 ItemComponents::placementFromState(slot));
        m_itemWorld.setAppearance(item->sessionId(), slot);
        return;
    }
    m_itemWorld.setPathState(item->path(), captureState(item));
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
    // Legacy live flip flags (should be empty after bake).
    if (item->itemHFlip() || item->itemVFlip()) {
        Qt::Orientations axes;
        if (item->itemHFlip()) {
            axes |= Qt::Horizontal;
        }
        if (item->itemVFlip()) {
            axes |= Qt::Vertical;
        }
        img = img.flipped(axes);
    }
    // Live grade only when display is still unbaked host (no applied xform).
    // Re-applying on a materialize bake double-grades the filmstrip override.
    if (!item->hasAppliedContentXform()) {
        const ColorAdjustments adj = item->colorAdjustments();
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
    if (sid != kInvalidSessionImageId) {
        app = m_itemWorld.getAppearance(sid);
    }
    if ((!app || !SessionAppearance::hasContentAppearance(*app)) && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            fallback = *st;
            app = &fallback;
        }
    }
    // Durable XDG appearance when session store is empty (slideshow may paint a
    // path before installDisplayPixels seeds m_appearance for that id).
    // Orient/flip only — never adopt path crop into an id-keyed soft paint.
    if ((!app || !SessionAppearance::hasContentAppearance(*app)) && !path.isEmpty()) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored)
            && (stored.contentHFlip || stored.contentVFlip
                || stored.contentQuarterTurns != 0)) {
            fallback = {};
            fallback.path = path;
            fallback.sessionId = sid;
            fallback.contentHFlip = stored.contentHFlip;
            fallback.contentVFlip = stored.contentVFlip;
            fallback.contentQuarterTurns = stored.contentQuarterTurns;
            app = &fallback;
        }
    }
    if (!app || !SessionAppearance::hasContentAppearance(*app)) {
        return src;
    }
    // Single pipeline — SoftPreview scales crop into soft pixel space
    // (SessionAppearance::materializeDisplay contract). Never strip crop.
    const QImage out = SessionAppearance::materializeDisplay(
        src, *app, SessionAppearance::PixelKind::SoftPreview);
    return out.isNull() ? src : out;
}


bool ImageView::hasSessionAppearance(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_itemWorld.hasAppearance(id);
}


void ImageView::setSessionAppearance(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    // Phase 7 Stage 1: dual-write crop/attention sparse tables via ItemWorld.
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
            item->setSessionId(sid);
        }
        WorkspaceItemState slot = captureState(item);
        slot.sessionId = sid;
        slot.sessionIndex = item->sessionIndex();
        slot.path = item->path();
        // ContentXform applied fingerprint wins. Do NOT resurrect prev turns
        // when capture says 0 — that is a real full-circle identity and was
        // the Gallery 4th-rotate corruption path.
        if (item->hasAppliedContentXform()) {
            const ContentXform::Value x = item->appliedContentXform();
            slot.contentQuarterTurns = x.quarterTurns;
            slot.contentHFlip = x.hFlip;
            slot.contentVFlip = x.vFlip;
            if (x.hasCrop) {
                slot.hasCrop = true;
                slot.cropRect = x.cropRect;
                slot.cropSourceSize = x.cropSourceSize;
                slot.cropRotation = x.cropRotation;
            }
        }
        m_itemWorld.setAppearance(sid, slot);
        contentSlot = slot;
        haveContentSlot = true;
    } else {
        // Unbound tile: still persist content-hash state for the file.
        contentSlot = captureState(item);
        contentSlot.contentHFlip = item->contentHFlip();
        contentSlot.contentVFlip = item->contentVFlip();
        contentSlot.hasCrop = item->sessionHasCrop();
        contentSlot.cropRect = item->sessionCropRect();
        haveContentSlot = true;
    }
    if (haveContentSlot) {
        // Durable local state (XDG_STATE_HOME/thumtoo): content-hash keyed.
        // Bound: orient/flip only — crop lives in SessionAppearanceStore by id.
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
        // share a path). Placement remains in m_itemStateBook.byPath from Workspace
        // rememberItemState / snapshot only.
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            // Id-keyed only — path signals paint every filmstrip row with
            // the same file (IDENTITY.md).
            emit sessionAppearanceChanged(sid, item->path(), appearance);
            emit sessionCropApplied(sid, item->path(), appearance,
                                    item->sessionHasCrop());
        }
    }
}
