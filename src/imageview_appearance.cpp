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


void ImageView::attachDisplaySample(ImageItem *item, const QImage &display,
                                      const WorkspaceItemState &want,
                                      SessionAppearance::PixelKind kind)
{
    if (!item || display.isNull()) {
        return;
    }
    const QString path = item->path();

    // Display samples are always display-ready (materializeDisplay or host-raw
    // identity). Never use setSourceImage here — it re-runs updateDisplayedPixmap
    // and double-applies m_colorAdjust on Gallery/Workspace tiles.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        item->setPreviewImage(display);
    } else {
        item->setSourceImageReady(display);
    }

    // Layout: ONE rule — ContentXform::layoutSize(fileNative, want).
    // Soft/full sample pixels never define intrinsic (SIZE.md / CONTENT_PIPELINE).
    // The old "crop → display.size()" branch set soft crop pixels as geometry,
    // which collapsed Workspace scale to ~1% and broke second-crop draft size.
    applyContentLayoutSize(item, want);
    // SIZE.md: samples (LQIP / soft / full ladder) never write intrinsic.
    // Cold open keeps the provisional stand-in until sizeReady / cachedSize.
    // Adopting display.size() made 32× LQIP the layout box, then jumped when
    // the durable probe arrived.
    if (qEnvironmentVariableIsSet("BILTOO_DEBUG_CROP")
        || (want.hasCrop && item->imageSize().width() <= 1)) {
        const QSize isz = item->imageSize();
        if (want.hasCrop && (isz.width() <= 1 || isz.height() <= 1)) {
            qCritical("attachDisplaySample: crop want but intrinsic %dx%d (display %dx%d path=%s)",
                      isz.width(), isz.height(), display.width(), display.height(),
                      qPrintable(path));
        }
    }

    item->setContentHFlip(want.contentHFlip);
    item->setContentVFlip(want.contentVFlip);
    item->setSessionCrop(want.hasCrop, want.cropRect);
    item->setColorAdjustmentsRecord(want.colorAdjust);
    item->setAppliedContentXform(ContentXform::Value::fromState(want));
}


void ImageView::rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Crop draft owns the live sample — pure rematerialize must not soft↔full.
    if (isCropDraftLockedItem(item)) {
        return;
    }
    if (tryRematerializeFromHost(item, want)) {
        return;
    }
    const QString path = item->path();
    // Host must be unoriented. Never bake from preview/display — that double-applies
    // crop when the tile already shows a soft crop (Gallery after Image crop).
    QImage raw = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (raw.isNull() && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        // FullSource without applied xform is still host-shaped (rare).
        raw = item->sourceImage();
    }
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (raw.isNull()) {
        // No unoriented host: schedule async; do not claim applied yet.
        if (!path.isEmpty() && SessionAppearance::hasContentAppearance(want)) {
            scheduleAsyncHostRematerialize(path, sid, want);
        }
        return;
    }
    const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
    int edge = ContentXform::longEdge(raw.size());
    QImage host = raw;
    SessionAppearance::PixelKind bakeKind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    bool scheduleFull = false;
    if (edge > maxGui) {
        // Soft stand-in now (crop/orient visible); full bake async.
        host = ImageCache::clampToMaxEdge(raw, maxGui);
        edge = ContentXform::longEdge(host.size());
        bakeKind = SessionAppearance::PixelKind::SoftPreview;
        scheduleFull = true;
    }
    if (edge <= 0 || edge > maxGui) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    const QImage display = SessionAppearance::materializeDisplay(host, want, bakeKind);
    if (display.isNull()) {
        scheduleAsyncHostRematerialize(path, sid, want);
        return;
    }
    if (bakeKind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        item->clearDecodedPixels();
    }
    attachDisplaySample(item, display, want, bakeKind);
    if (scheduleFull) {
        scheduleAsyncHostRematerialize(path, sid, want);
    }
}


void ImageView::rematerializeGalleryItemFromStore(ImageItem *item)
{
    if (!item) {
        return;
    }
    const SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId) {
        return;
    }
    const WorkspaceItemState *st = appearance().get(sid);
    if (!st || !SessionAppearance::hasContentAppearance(*st)) {
        return;
    }
    const ContentXform::Value want = ContentXform::Value::fromState(*st);
    const ContentXform::Value applied = item->hasAppliedContentXform()
        ? item->appliedContentXform()
        : ContentXform::Value{};
    if (ContentXform::equal(applied, want) && item->hasDisplayPixels()) {
        // Applied matches store; still fix layout if intrinsic is full-frame.
        applyContentLayoutSize(item, *st);
        return;
    }
    rematerializeItemContent(item, *st);
}


bool ImageView::tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return false;
    }
    if (isCropDraftLockedItem(item)) {
        return false;
    }
    const QString path = item->path();
    const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull()) {
        return false;
    }
    if (ContentXform::longEdge(host.size()) > ContentXform::kGuiMaterializeMaxEdge) {
        return false;
    }
    // Prefer SoftPreview when the live item is soft-only so setPreviewImage
    // accepts the sample (setPreviewImage ignores soft when full is present).
    // If full is already shown, rematerialize as FullSource.
    const auto kind = item->hasDecodedPixels()
        ? SessionAppearance::PixelKind::FullSource
        : SessionAppearance::PixelKind::SoftPreview;
    const QImage display =
        SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        return false;
    }
    attachDisplaySample(item, display, want, kind);
    applyContentLayoutSize(item, want);
    return true;
}


void ImageView::applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Intrinsic is always ContentXform layout of file-native size — never sample
    // pixel dimensions. Using displayImage().size() for crops shrank Workspace
    // tiles to soft resolution (and stretched wrong pixels on re-crop).
    const QString path = item->path();
    QSize fileNative = logicalSizeForPath(path);
    if (!isPositiveSize(fileNative) || fileNative.width() <= 1 || fileNative.height() <= 1
        || (!path.isEmpty() && isProvisionalImageSize(path))) {
        // Fall back: crop rect in recorded source space, or orient-only current.
        if (want.hasCrop && !want.cropRect.isEmpty()) {
            const QSize basis = (want.cropSourceSize.isValid()
                                && want.cropSourceSize.width() > 1)
                                   ? want.cropSourceSize
                                   : item->imageSize();
            const QRect c = SessionAppearance::scaleCropRect(
                want.cropRect.normalized(), want.cropSourceSize, basis);
            if (c.width() > 1 && c.height() > 1) {
                item->setIntrinsicSize(c.size());
            }
        }
        return;
    }
    const QSize lay = ContentXform::layoutSize(fileNative, want);
    if (isPositiveSize(lay) && lay.width() > 1 && lay.height() > 1) {
        item->setIntrinsicSize(lay);
    }
}


void ImageView::scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                                 const WorkspaceItemState &want)
{
    if (path.isEmpty()) {
        return;
    }
    if (isCropDraftLockedPath(path)) {
        return;
    }
    const QImage hostProbe = ImageCache::get(path);
    if (hostProbe.isNull()) {
        return;
    }
    if (ContentXform::longEdge(hostProbe.size())
        <= ContentXform::kGuiMaterializeMaxEdge) {
        return; // GUI path already handled by tryRematerializeFromHost
    }
    const quint64 gen = m_displayPipeline.loadGate().generation();
    QPointer<ImageView> guard(this);
    const WorkspaceItemState wantCopy = want;
    QThreadPool::globalInstance()->start([guard, path, sid, wantCopy, gen]() {
        if (!guard) {
            return;
        }
        const QImage host = ImageCache::get(path);
        if (host.isNull()) {
            return;
        }
        // Worker thread: multi-MP materialize is allowed.
        const QImage display = SessionAppearance::materializeDisplay(
            host, wantCopy, SessionAppearance::PixelKind::FullSource);
        if (display.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, path, sid, wantCopy, display, gen]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            guard->finishAsyncHostRematerialize(path, sid, wantCopy, display);
        }, Qt::QueuedConnection);
    });
}


void ImageView::finishAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                               const WorkspaceItemState &want,
                                               const QImage &display)
{
    ASSERT_GUI_THREAD();
    if (display.isNull() || path.isEmpty()) {
        return;
    }
    // Crop draft owns the target item — do not reinstall over orient-only draft.
    if (isCropDraftLockedPath(path)) {
        return;
    }
    ImageItem *item = nullptr;
    for (ImageItem *it : m_items) {
        if (!it || it->path() != path) {
            continue;
        }
        if (sid != kInvalidSessionImageId && it->sessionId() != sid
            && it->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        item = it;
        break;
    }
    if (!item) {
        return;
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);
    // Discard stale worker result if the store moved on for this session id.
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *cur = appearance().get(sid)) {
            if (!ContentXform::equal(ContentXform::Value::fromState(*cur), wantX)) {
                return;
            }
        }
    }
    // Already settled FullSource for this want at ≥ this resolution — skip.
    // Soft (or soft-sized) FullSource with applied identity used to match and
    // discard Prefer/Full async bakes permanently.
    if (item->hasDecodedPixels() && item->hasAppliedContentXform()
        && ContentXform::equal(item->appliedContentXform(), wantX)
        && !item->shouldUpgradeDisplayTo(ImageCache::longEdge(display))) {
        return;
    }
    const QSize before = item->imageSize();
    attachDisplaySample(item, display, want, SessionAppearance::PixelKind::FullSource);
    applyContentLayoutSize(item, want);
    if (before != item->imageSize()) {
        preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
    }
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }
    if (isGalleryMode() && before != item->imageSize()) {
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
    if (isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    } else if (isImageMode()) {
        driveImageFocusSurface();
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::bakeItemRotate90(ImageItem *item, int quarterTurns)
{
    if (!item || quarterTurns == 0) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    const SessionImageId sid = resolveContentEditSessionId(item);
    const int turns = ContentXform::normalizeQuarterTurns(
        beforeSt.contentQuarterTurns + quarterTurns);
    // Keep full-source crop geometry in sync with content orientation so
    // re-entering crop mode still frames the same region.
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentRotate90(cropMap, quarterTurns);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    // Absolute want after this edit.
    WorkspaceItemState want = beforeSt;
    want.contentQuarterTurns = turns;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentHFlip = item->contentHFlip();
    want.contentVFlip = item->contentVFlip();
    want.colorAdjust = item->colorAdjustments();

    // ContentXform is ground truth: absolute want from store + delta, pure
    // materialize from unoriented host. Never stack incremental transforms.
    // ≤512 host: GUI pure. Multi-MP: soft stand-in from clamped host + async.
    if (!tryRematerializeFromHost(item, want)) {
        const QString path = item->path();
        const QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
        bool gotDisplay = false;
        if (!host.isNull()) {
            QImage soft = host;
            if (ContentXform::longEdge(host.size())
                > ContentXform::kGuiMaterializeMaxEdge) {
                soft = ImageCache::clampToMaxEdge(
                    host, ContentXform::kGuiMaterializeMaxEdge);
            }
            const QImage display = SessionAppearance::materializeDisplay(
                soft, want, SessionAppearance::PixelKind::SoftPreview);
            if (!display.isNull()) {
                // Soft attach must not be ignored when full was present.
                item->clearDecodedPixels();
                attachDisplaySample(item, display, want,
                                    SessionAppearance::PixelKind::SoftPreview);
                gotDisplay = true;
            }
        }
        if (!gotDisplay) {
            // No host at all: last resort incremental on whatever is shown.
            item->bakeRotate90(quarterTurns);
        }
        applyContentLayoutSize(item, want);
        scheduleAsyncHostRematerialize(path, sid, want);
    } else {
        applyContentLayoutSize(item, want);
    }

    // Write ContentXform absolute state first — before commitItemSessionEdit
    // captureState, so commit cannot resurrect stale path-map turns.
    {
        WorkspaceItemState s = want;
        s.sessionId = sid;
        s.path = item->path();
        s.orientation = 0.0;
        s.contentQuarterTurns = turns;
        if (sid != kInvalidSessionImageId) {
            // Preserve placement fields from previous appearance when present.
            if (const WorkspaceItemState *prev = appearance().get(sid)) {
                s.pos = prev->pos;
                s.scale = prev->scale;
                s.scaleY = prev->scaleY;
                s.shear = prev->shear;
                s.rotation = prev->rotation;
                s.opacity = prev->opacity;
                s.z = prev->z;
                s.hFlip = prev->hFlip;
                s.vFlip = prev->vFlip;
                s.sessionIndex = prev->sessionIndex;
            }
            appearance().set(sid, s);
            persistDurableContentAppearance(item, s, "bakeRotate");
        }
        // Keep path map content fields in sync so pack afterEach cannot leave
        // stale turns for any reader that still peeks at m_itemStateBook.byPath.
        {
            WorkspaceItemState pathSlot;
            if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
                pathSlot = *st;
            }
            pathSlot.path = item->path();
            pathSlot.contentQuarterTurns = turns;
            pathSlot.contentHFlip = want.contentHFlip;
            pathSlot.contentVFlip = want.contentVFlip;
            pathSlot.hasCrop = want.hasCrop;
            pathSlot.cropRect = want.cropRect;
            pathSlot.cropRotation = want.cropRotation;
            pathSlot.cropSourceSize = want.cropSourceSize;
            m_itemStateBook.set(item->path(), pathSlot);
        }
        item->setAppliedContentXform(ContentXform::Value::fromState(s));
    }

    commitItemSessionEdit(item);

    // Image mode: contentRect axes may have swapped — refresh tight sceneRect
    // so pan/fit are not locked to the pre-rotate box.
    if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = item->contentHFlip();
    afterSt.contentVFlip = item->contentVFlip();
    afterSt.contentQuarterTurns = turns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(tr("Rotate"), item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
}


void ImageView::bakeItemFlip(ImageItem *item, bool horizontal, bool vertical)
{
    if (!item || (!horizontal && !vertical)) {
        return;
    }
    const QImage beforeSrc = item->sourceImage().copy();
    WorkspaceItemState beforeSt = captureContentBakeBeforeState(item);

    // Content-orientation flags track the source raster so durable crop mapping
    // (flip then quarter-turn on the full raster) stay consistent with the
    // display-space flip just applied to the oriented pixels.
    //
    // With quarter-turns t: display H/V axes map to source axes as
    //   t=0,2: same axes;  t=1,3: H↔V  (conjugation through 90°/270° CW).
    // Crop stays in post-content space, so mapCropThroughContentFlip below
    // still uses the display axes the user pressed.
    bool h = beforeSt.contentHFlip;
    bool v = beforeSt.contentVFlip;
    int turns = beforeSt.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    const bool swapAxes = (turns == 1 || turns == 3);
    const bool srcH = swapAxes ? vertical : horizontal;
    const bool srcV = swapAxes ? horizontal : vertical;
    if (srcH) {
        h = !h;
    }
    if (srcV) {
        v = !v;
    }

    const SessionImageId sid = resolveContentEditSessionId(item);
    WorkspaceItemState cropMap = appearanceCropMapForEdit(item, beforeSt, sid);
    SessionAppearance::mapCropThroughContentFlip(cropMap, horizontal, vertical);
    if (cropMap.hasCrop) {
        item->setSessionCrop(true, cropMap.cropRect);
    }

    WorkspaceItemState want = beforeSt;
    want.contentHFlip = h;
    want.contentVFlip = v;
    want.hasCrop = cropMap.hasCrop;
    want.cropRect = cropMap.cropRect;
    want.cropRotation = cropMap.cropRotation;
    want.cropSourceSize = cropMap.cropSourceSize;
    want.contentQuarterTurns = cropMap.contentQuarterTurns;

    // Prefer pure rematerialize from unoriented host; else incremental + async.
    item->setContentHFlip(h);
    item->setContentVFlip(v);
    if (!tryRematerializeFromHost(item, want)) {
        item->bakeFlip(horizontal, vertical);
        item->setContentHFlip(h);
        item->setContentVFlip(v);
        scheduleAsyncHostRematerialize(item->path(), sid, want);
    }
    applyContentLayoutSize(item, want);

    if (sid != kInvalidSessionImageId) {
        WorkspaceItemState s = captureState(item);
        s.sessionId = sid;
        s.hasCrop = cropMap.hasCrop;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        s.contentQuarterTurns = cropMap.contentQuarterTurns;
        appearance().set(sid, s);
        persistDurableContentAppearance(item, s, "bakeFlip");
    } else if (cropMap.hasCrop) {
        WorkspaceItemState s = captureState(item);
        s.hasCrop = true;
        s.cropRect = cropMap.cropRect;
        s.cropRotation = cropMap.cropRotation;
        s.cropSourceSize = cropMap.cropSourceSize;
        s.contentHFlip = h;
        s.contentVFlip = v;
        m_itemStateBook.set(item->path(), s);
    }

    commitItemSessionEdit(item);

    {
        WorkspaceItemState tag;
        tag.contentHFlip = h;
        tag.contentVFlip = v;
        tag.contentQuarterTurns = beforeSt.contentQuarterTurns;
        tag.hasCrop = item->sessionHasCrop();
        tag.cropRect = item->sessionCropRect();
        tag.cropRotation = cropMap.cropRotation;
        tag.cropSourceSize = cropMap.cropSourceSize;
        item->setAppliedContentXform(ContentXform::Value::fromState(tag));
    }

    WorkspaceItemState afterSt = captureState(item);
    afterSt.hasCrop = item->sessionHasCrop();
    afterSt.cropRect = item->sessionCropRect();
    afterSt.cropRotation = cropMap.cropRotation;
    afterSt.cropSourceSize = cropMap.cropSourceSize;
    afterSt.contentHFlip = h;
    afterSt.contentVFlip = v;
    afterSt.contentQuarterTurns = beforeSt.contentQuarterTurns;
    afterSt.sessionId = beforeSt.sessionId;
    pushItemContentCommand(horizontal && !vertical ? tr("Flip horizontal")
                          : vertical && !horizontal ? tr("Flip vertical")
                          : tr("Flip"),
                           item, beforeSrc, item->sourceImage().copy(),
                           beforeSt, afterSt);
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


void ImageView::applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want)
{
    if (!item) {
        return;
    }
    // Prefer pure live grade when the tile still holds unbaked host pixels
    // (no orient/crop/grade bake). Avoids materializeDisplay on the GUI.
    const bool contentGeom = want.hasCrop || want.contentHFlip || want.contentVFlip
        || want.contentQuarterTurns != 0;
    if (!contentGeom && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        item->setColorAdjustments(want.colorAdjust);
        const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
            ? item->sessionId()
            : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
        if (sid != kInvalidSessionImageId) {
            const QImage appearance = sessionAppearanceImage(item);
            if (!appearance.isNull()) {
                emit sessionAppearanceChanged(sid, item->path(), appearance);
            }
        }
        return;
    }

    const QString path = item->path();
    QImage host = path.isEmpty() ? QImage() : ImageCache::get(path);
    if (host.isNull() && item->hasDecodedPixels() && !item->hasAppliedContentXform()) {
        host = item->sourceImage();
    }
    if (host.isNull()) {
        item->setColorAdjustmentsRecord(want.colorAdjust);
        item->update();
        return;
    }
    // materializeDisplay asserts NOT GUI when long edge > kGuiMaterializeMaxEdge.
    // Interactive path must stay ≤ that limit (full bake is async on commit).
    const int kInteractiveGradeMaxEdge = ContentXform::kGuiMaterializeMaxEdge;
    if (ImageCache::longEdge(host) > kInteractiveGradeMaxEdge) {
        host = ImageCache::clampToMaxEdge(host, kInteractiveGradeMaxEdge);
    }
    const auto kind = SessionAppearance::PixelKind::SoftPreview;
    const QImage display = SessionAppearance::materializeDisplay(host, want, kind);
    if (display.isNull()) {
        item->setColorAdjustmentsRecord(want.colorAdjust);
        return;
    }
    if (item->hasDecodedPixels()
        && ImageCache::longEdge(item->sourceImage()) > kInteractiveGradeMaxEdge) {
        // Soft stand-in for the drag; keep session id / path on the item.
        item->clearDecodedPixels();
    }
    attachDisplaySample(item, display, want, kind);
    // Filmstrip / Gallery chrome: push soft appearance while dragging so the
    // strip does not wait for the idle commit (and does not require FullSource).
    const SessionImageId sid = item->sessionId() != kInvalidSessionImageId
        ? item->sessionId()
        : (isImageMode() ? m_sessionId.currentIdValue() : kInvalidSessionImageId);
    if (sid != kInvalidSessionImageId) {
        const QImage appearance = sessionAppearanceImage(item);
        if (!appearance.isNull()) {
            emit sessionAppearanceChanged(sid, item->path(), appearance);
        }
    }
}


void ImageView::scheduleColorAdjustCommit(SessionImageId sid, const QString &path)
{
    m_colorAdjustCommit.schedule(sid, path);
    if (m_colorAdjustCommitTimer) {
        m_colorAdjustCommitTimer->start();
    } else {
        flushColorAdjustCommit();
    }
}


void ImageView::flushColorAdjustCommit()
{
    SessionImageId sid = kInvalidSessionImageId;
    QString path;
    if (!m_colorAdjustCommit.take(&sid, &path)) {
        return;
    }
    ImageItem *item = nullptr;
    if (sid != kInvalidSessionImageId) {
        for (ImageItem *it : m_items) {
            if (it && it->sessionId() == sid) {
                item = it;
                break;
            }
        }
    }
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    WorkspaceItemState want;
    if (sid != kInvalidSessionImageId && appearance().contains(sid)) {
        want = appearance().value(sid);
    } else if (item) {
        want = captureState(item);
        want.colorAdjust = item->colorAdjustments();
    } else {
        return;
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; colour commit waits until crop exits.
    if (isCropDraftLockedItem(item)) {
        return;
    }
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into thumtoo durable appearance — SessionAppearanceStore / project
    // already own it; path cache is for orient/crop hints, not slider spam.
    rematerializeItemContent(item, want);
    // Gallery: same session id may be stashed while Image mode edits — the
    // live tile update covers Image/Gallery focus; filmstrip uses the emit.
    const QImage appearance = sessionAppearanceImage(item);
    if (!appearance.isNull()) {
        const SessionImageId emitSid = sid != kInvalidSessionImageId
            ? sid
            : item->sessionId();
        if (emitSid != kInvalidSessionImageId) {
            emit sessionAppearanceChanged(emitSid,
                                          item->path().isEmpty() ? path : item->path(),
                                          appearance);
        }
    }
}


void ImageView::setTargetColorAdjustments(const ColorAdjustments &adj)
{
    ImageItem *item = targetItem();
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return;
    }
    SessionImageId sid = item->sessionId();
    if (sid == kInvalidSessionImageId && isImageMode()) {
        sid = m_sessionId.currentIdValue();
    }
    WorkspaceItemState slot = (sid != kInvalidSessionImageId && appearance().contains(sid))
        ? appearance().value(sid)
        : captureState(item);
    if (sid != kInvalidSessionImageId) {
        slot.sessionId = sid;
        slot.path = item->path();
        slot.colorAdjust = adj;
        appearance().set(sid, slot);
    } else {
        slot.colorAdjust = adj;
    }
    // Fast path while dragging: bake from clamped host (no SQLite / filmstrip).
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
    emit statusChanged();
}

