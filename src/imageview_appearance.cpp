// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Session appearance load/store/apply + Apply-undo (IDENTITY.md).
// Crop draft enter/leave/bake stays in imageview_crop.cpp.

#include "imageview.h"
#include "cropsession.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "thumtoocache.h"

#include "imageitem.h"
#include "sessionappearance.h"
#include "thumtoocache.h"

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
        if (const WorkspaceItemState *it = appearance().get(sid)) {
            return it;
        }
        // Bound with no durable content after seed = full frame.
        // NEVER fall back to the path map — that leaks crop/flip across
        // independent session images that share a file path.
        return nullptr;
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
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

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        appearance().set(sid, s);
    } else {
        // Unbound only: path map is the sole store.
        m_itemStateBook.set(item->path(), s);
    }
}

bool ImageView::loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const
{
    if (!st || sid == kInvalidSessionImageId) {
        return false;
    }
    if (const WorkspaceItemState *it = appearance().get(sid)) {
        *st = *it;
        return true;
    }
    return false;
}

bool ImageView::loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                          SessionImageId *sidOut) const
{
    if (!item || !app) {
        return false;
    }
    const SessionImageId sid = CropSession::resolveSessionIdForItem(
        item, m_sessionId.currentIdValue());
    if (sidOut) {
        *sidOut = sid;
    }
    if (loadSessionAppearance(sid, app)) {
        return true;
    }
    if (CropSession::fillAppearanceFromItemSessionCrop(app, item)) {
        return true;
    }
    if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
        *app = *st;
        return true;
    }
    return false;
}


void ImageView::restoreSessionCropAppearance(ImageItem *item)
{
    if (!item) {
        return;
    }
    WorkspaceItemState app;
    SessionImageId sid = kInvalidSessionImageId;
    if (!loadRestoreCropAppearance(item, &app, &sid)) {
        return;
    }
    const QString path = item->path();
    const QImage full = fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
    } else if (!tryRematerializeFromHost(item, app)) {
        installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        if (!ContentXform::equal(
                item->hasAppliedContentXform() ? item->appliedContentXform()
                                               : ContentXform::Value{},
                ContentXform::Value::fromState(app))) {
            rematerializeItemContent(item, app);
        }
    }
    fitImageOrUpdateWorkspace(item);
}

void ImageView::applyCropAppearance(ImageItem *item, const QImage &src,
                                    const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Undo/redo after-image: pixels are already content-baked — attach only.
    if (!src.isNull()) {
        attachDisplaySample(item, src, state, SessionAppearance::PixelKind::FullSource);
    } else {
        item->setSessionCrop(state.hasCrop, state.cropRect);
        item->setContentHFlip(state.contentHFlip);
        item->setContentVFlip(state.contentVFlip);
        item->setAppliedContentXform(ContentXform::Value::fromState(state));
    }
    applyState(item, state);
    // Seed appearance with the full state (including cropRotation) before
    // commitItemSessionEdit, which rebuilds the slot via captureState.
    {
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }
        WorkspaceItemState slot = state;
        slot.sessionId = sid;
        slot.path = item->path();
        storeCropAppearance(item, sid, slot);
    }
    // Appearance persistence is commitItemSessionEdit → m_appearance (by id).
    // Do not write crop state into the path map for bound tiles.
    commitItemSessionEdit(item);
    // Undo back to identity: commit no longer writes identity (avoids wiping
    // good rows on noisy commits), so clear durable state explicitly.
    if (!SessionAppearance::hasContentAppearance(state)) {
        ThumtooCache::clearContentAppearance(item->path());
    }
    if (isImageMode()) {
        m_framing.armFit();
        fitItem(item, currentFitAspectMode());
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    }
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                           ImageItem *item, const QImage &preferredDisplay,
                                           bool hasCrop)
{
    if (sid == kInvalidSessionImageId) {
        return;
    }
    // Prefer the crop bake just materialized when provided — not displayImage(),
    // which can still be pre-crop if soft attach was rejected.
    QImage appearance = preferredDisplay;
    if (appearance.isNull() && item) {
        appearance = sessionAppearanceImage(item);
    }
    if (appearance.isNull()) {
        return;
    }
    if (hasCrop) {
        emit sessionAppearanceChanged(sid, path, appearance);
    }
    emit sessionCropApplied(sid, path, appearance, hasCrop);
}



// --- from imageview_layout.cpp (appearance) ---

void ImageView::applyState(ImageItem *item, const WorkspaceItemState &state)
{
    item->setPos(state.pos);
    item->setItemScale(state.scale, state.scaleY > 0.0 ? state.scaleY : state.scale);
    item->setItemShear(state.shear);
    item->setItemRotation(state.rotation);
    item->setItemOpacity(state.opacity);
    item->setStackZ(state.z);
    item->setItemHFlip(state.hFlip);
    item->setItemVFlip(state.vFlip);
    // Content pixels/applied are set at install (installDisplayPixels / attachDisplaySample),
    // not here — otherwise a second apply would crop already-cropped display.
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
        const WorkspaceItemState *prev = m_itemStateBook.get(item->path());
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
        m_itemStateBook.set(item->path(), s);
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
        appearance().set(item->sessionId(), slot);
        return;
    }
    m_itemStateBook.set(item->path(), captureState(item));
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
        app = appearance().get(sid);
    }
    if ((!app || !SessionAppearance::hasContentAppearance(*app)) && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
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
    return id != kInvalidSessionImageId && appearance().contains(id);
}


void ImageView::setSessionAppearance(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    appearance().set(id, state);
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
        appearance().set(sid, slot);
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


void ImageView::syncSessionEditPeers(ImageItem *item)
{
    // Propagate pixel / flip / orientation session edits to matching canvas and
    // stashed instances. Placement (pos, scale, free tilt) is preserved.
    const QString path = item->path();
    // Strict identity: only a valid SessionImageId. Never m_sessionId.currentIdValue()
    // fallback here — that would push this item's pixels onto another tile.
    const SessionImageId sessionId = item->sessionId();
    const QImage src = item->sourceImage();
    const bool hFlip = item->itemHFlip();
    const bool vFlip = item->itemVFlip();

    QList<ImageItem *> peers;
    auto collect = [&](const QList<ImageItem *> &list) {
        for (ImageItem *other : list) {
            if (other && other != item) {
                peers.append(other);
            }
        }
    };
    collect(m_items);
    collect(m_workspace.stashedItems());
    collect(m_gallery.stashedItems());

    auto shouldSync = [&](ImageItem *other) -> bool {
        if (!other || other == item) {
            return false;
        }
        // Same stable session-image id only. Path / list-index must never merge
        // independent duplicates on the Workspace.
        if (sessionId == kInvalidSessionImageId || other->sessionId() != sessionId) {
            return false;
        }
        if (other->path() != path) {
            qCritical("commitItemSessionEdit: SessionImageId %lld bound to different paths (%s vs %s) — refusing peer sync",
                      static_cast<long long>(sessionId),
                      qPrintable(path), qPrintable(other->path()));
            return false;
        }
        return true;
    };

    auto syncOne = [&](ImageItem *other) {
        if (!shouldSync(other)) {
            return;
        }
        // Already-baked display from the edited peer. Must replace peers fully:
        // setPreviewImage is a no-op when the peer still holds full m_source
        // (Gallery stash after Image crop). That left crop intrinsic + full
        // pixels for one frame / until next soft install (Gallery return glitch).
        const QImage baked = !src.isNull() ? src
            : (!item->previewImage().isNull() ? item->previewImage()
                                              : item->displayImage());
        if (!baked.isNull()) {
            other->clearDecodedPixels();
            // Already-baked display from the editor. Attach via the same gate
            // as install (layout + applied + chrome) — do not put into ImageCache.
            WorkspaceItemState want;
            if (sessionId != kInvalidSessionImageId) {
                if (const WorkspaceItemState *st = appearance().get(sessionId)) {
                    want = *st;
                }
            }
            if (!SessionAppearance::hasContentAppearance(want) && item->hasAppliedContentXform()) {
                ContentXform::Value x = item->appliedContentXform();
                x.applyToState(want);
            }
            const auto kind = !src.isNull()
                ? SessionAppearance::PixelKind::FullSource
                : SessionAppearance::PixelKind::SoftPreview;
            attachDisplaySample(other, baked, want, kind);
        } else if (sessionId != kInvalidSessionImageId) {
            if (const WorkspaceItemState *st = appearance().get(sessionId)) {
                applyContentLayoutSize(other, *st);
            }
        }
        other->setItemHFlip(hFlip);
        other->setItemVFlip(vFlip);
    };
    for (ImageItem *other : peers) {
        syncOne(other);
    }
}


void ImageView::updateWorkspaceSavedAppearance(ImageItem *item)
{
    // Durable snapshot: update the entry for this session image id.
    const SessionImageId sessionId = item->sessionId();
    if (sessionId == kInvalidSessionImageId) {
        return;
    }
    const WorkspaceItemState *st = appearance().get(sessionId);
    if (!st) {
        return;
    }
    const QString path = item->path();
    const bool hFlip = item->itemHFlip();
    const bool vFlip = item->itemVFlip();
    for (WorkspaceItemState &slot : m_workspace.savedItems()) {
        if (slot.sessionId != sessionId) {
            continue;
        }
        slot.hasCrop = st->hasCrop;
        slot.cropRect = st->cropRect;
        slot.hFlip = hFlip;
        slot.vFlip = vFlip;
        slot.contentQuarterTurns = st->contentQuarterTurns;
        slot.contentHFlip = st->contentHFlip;
        slot.contentVFlip = st->contentVFlip;
        slot.orientation = 0.0;
        slot.sessionId = sessionId;
        slot.path = path;
    }
}


void ImageView::commitItemSessionEdit(ImageItem *item)
{
    if (!item) {
        return;
    }
    rememberItemState(item);
    persistSessionAppearanceSlot(item);
    validateUniqueLiveSessionIds("commitItemSessionEdit");
    syncSessionEditPeers(item);
    updateWorkspaceSavedAppearance(item);
    // All modes / widgets that depend on content aspect or appearance pixels.
    propagateSessionAppearanceToViews(item);
    emit statusChanged();
}


void ImageView::propagateSessionAppearanceToViews(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Filmstrip: persistSessionAppearanceSlot already emits when display pixels
    // exist. Re-emit after peer sync so soft-only tiles that gained pixels, and
    // paths that skipped emit, still update the strip.
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        const QImage appearanceImage = sessionAppearanceImage(item);
        if (!appearanceImage.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearanceImage);
            if (item->sessionHasCrop()
                || (appearance().contains(sid)
                    && appearance().value(sid).hasCrop)) {
                emit sessionCropApplied(sid, item->path(), appearanceImage, /*hasCrop=*/true);
            }
        }
    }

    if (isGalleryMode()) {
        // Aspect / crop may change pack cell size — debounce packs concurrent
        // multi-select rotates into one layout pass.
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    } else if (isWorkspaceMode()) {
        updateWorkspaceSceneRect();
    } else if (isImageMode() && m_scene && item->scene() == m_scene) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
        if (viewport()) {
            viewport()->update();
        }
    }
}


void ImageView::copySessionAppearance(SessionImageId fromId, SessionImageId toId)
{
    if (fromId == kInvalidSessionImageId || toId == kInvalidSessionImageId
        || fromId == toId) {
        return;
    }
    // Prefer the session store; fall back to a live donor tile so drop-duplicate
    // from a graded filmstrip row still carries crop / bakes / colour grade.
    WorkspaceItemState dst;
    if (const WorkspaceItemState *src = appearance().get(fromId)) {
        dst = *src;
    } else {
        ImageItem *donor = findItemBySessionId(fromId);
        if (!donor && isImageMode()) {
            donor = primaryItem();
            if (donor && donor->sessionId() != fromId) {
                donor = nullptr;
            }
        }
        if (!donor) {
            return;
        }
        dst = captureState(donor);
    }
    dst.sessionId = toId;
    dst.pos = QPointF();
    dst.scale = 1.0;
    dst.scaleY = 1.0;
    dst.shear = 0.0;
    dst.rotation = 0.0;
    dst.opacity = 1.0;
    dst.z = 0.0;
    appearance().set(toId, dst);

    ImageItem *donor = findItemBySessionId(fromId);
    if (!donor && isImageMode()) {
        donor = primaryItem();
        if (donor && donor->sessionId() != fromId) {
            donor = nullptr;
        }
    }
    if (donor) {
        const QImage appearance = sessionAppearanceImage(donor);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(toId, dst.path, appearance);
            if (dst.hasCrop) {
                emit sessionCropApplied(toId, dst.path, appearance, /*hasCrop=*/true);
            }
        }
    }
}






// --- content appearance targets (from transform) ---

bool ImageView::targetHasContentAppearance() const
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return false;
    }
    for (const ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }
        if (sid != kInvalidSessionImageId) {
            if (const WorkspaceItemState *app = appearance().get(sid)) {
                if (SessionAppearance::hasContentAppearance(*app)) {
                    return true;
                }
            }
        }
        if (SessionAppearance::liveItemHasContentMods(
                item->sessionHasCrop(), item->contentHFlip(), item->contentVFlip())) {
            return true;
        }
        if (ThumtooCache::hasContentAppearance(item->path())) {
            return true;
        }
    }
    return false;
}


int ImageView::resetContentAppearanceForTargets()
{
    const QList<ImageItem *> targets = transformTargets();
    if (targets.isEmpty()) {
        return 0;
    }
    int n = 0;
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        const QString path = item->path();
        SessionImageId sid = item->sessionId();
        if (sid == kInvalidSessionImageId && isImageMode()) {
            sid = m_sessionId.currentIdValue();
        }

        // 1) Drop durable XDG state for this content.
        ThumtooCache::clearContentAppearance(path);

        // 2) Clear session appearance content fields (keep placement).
        if (sid != kInvalidSessionImageId) {
            WorkspaceItemState slot = SessionAppearance::clearedContentOps(
                appearance().value(sid));
            slot.sessionId = sid;
            slot.path = path;
            // Keep color grade / pose if present.
            appearance().set(sid, slot);
        }
        // Path map still holds content turns from prior bake/pack; captureState
        // re-merges turns==0 from m_itemStateBook.byPath and can resurrect orientation.
        if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
            WorkspaceItemState pathSlot = SessionAppearance::clearedContentOps(*st);
            m_itemStateBook.set(path, pathSlot);
        }

        item->setContentHFlip(false);
        item->setContentVFlip(false);
        item->setSessionCrop(false, QRect());
        item->setItemHFlip(false);
        item->setItemVFlip(false);

        // Restore layout geometry to the unoriented native size (content
        // rotate may have transposed intrinsic).
        {
            QSize native = ThumtooCache::cachedSize(path);
            if (!native.isValid() || native.width() < 1 || native.height() < 1) {
                const QSize known = m_sizeBook.known(path);
                if (!known.isEmpty()) {
                    native = known;
                }
            }
            if (native.isValid() && native.width() > 1 && native.height() > 1
                && native != QSize(1000, 1000) && native != QSize(1024, 1024)) {
                item->setIntrinsicSize(native);
            }
        }

        // 3) Reinstall *mode-appropriate* pixels — never promote a full decode
        // into Gallery soft tiles (that stuck tiles on native res and skipped
        // the soft ladder forever via hasDecodedPixels()).
        //
        //   Gallery  → soft ladder (≤ kGalleryLadderEdge), reset soft state
        //   Image / Workspace → full on-disk decode (user is inspecting / placing)
        //
        // Gallery focused tiles often already hold a *full* oriented decode
        // (Image-mode visit or soft→full upgrade). setPreviewImage no-ops when
        // full source is present, so identity soft would never replace the
        // oriented pixels — Gallery + filmstrip stayed flipped while Image mode
        // (FullSource install) looked correct. Always drop pixels first.
        if (isGalleryMode()) {
            gallerySoftResetPath(path);
            item->clearDecodedPixels();
            const int softEdge = ThumtooCache::kGalleryLadderEdge;
            QImage soft = ImageLoader::loadThumbnail(path, softEdge);
            if (!soft.isNull()) {
                // Identity appearance: SoftPreview install without content bake.
                installDisplayPixels(item, soft, SessionAppearance::PixelKind::SoftPreview,
                                     sid);
            }
            // else: decode window will refill after soft state reset
        } else {
            const QImage full = fullRasterForEdit(path);
            if (!full.isNull()) {
                installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource,
                                     sid);
            } else {
                item->clearDecodedPixels();
            }
        }

        if (isImageMode() && m_framing.isFitMode()) {
            fitItem(item, currentFitAspectMode());
        }

        // Filmstrip: emit current *display* pixels (soft in Gallery, full in Image).
        // Do not emit a separate full decode for Gallery filmstrip overrides.
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, path, appearance);
            }
        }
        ++n;
    }
    if (n > 0 && isGalleryMode()) {
        applyLayout(GalleryPackReason::ContentChange);
        // Soft state was reset; kick the ladder for visible tiles.
        updateGalleryDecodeWindow();
    }
    if (n > 0) {
        emit statusChanged();
    }
    return n;
}

