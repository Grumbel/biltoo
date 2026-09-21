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
    // Phase 7 Stage 2: applied ContentXform is the live fingerprint (survives
    // clearDecodedPixels; identity clear uses clearLiveContentMeta).
    // Dual-write ItemWorld runtime table when bound (Stage 2 residual 2080).
    const ContentXform::Value x = ContentXform::Value::fromState(state);
    item->setAppliedContentXform(x);
    const SessionImageId sid = item->sessionId();
    if (sid != kInvalidSessionImageId) {
        m_itemWorld.setAppliedContentXform(sid, x);
    }
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
    item->setSessionId(id);
    // Stage 2 residual: list-order cache follows document when the id is bound.
    refreshSessionIndexCache(item);
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
    if (item->sessionId() != kInvalidSessionImageId) {
        WorkspaceItemState slot = freezeItemAppearance(item);
        slot.sessionId = item->sessionId();
        slot.sessionIndex = sessionListIndex(item);
        slot.path = item->path();
        m_itemWorld.setAppearance(item->sessionId(), slot);
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
    if (sid != kInvalidSessionImageId && m_itemWorld.hasDurableAppearance(sid)) {
        fallback = sessionAppearanceValue(sid);
        app = &fallback;
    }
    // Unbound only: path map may hold content. Bound content is sparse + XDG
    // (setPathState strips content when sessionId is set — tip 2069).
    if ((!app || !SessionAppearance::hasContentAppearance(*app))
        && sid == kInvalidSessionImageId && !path.isEmpty()) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(path)) {
            fallback = *st;
            app = &fallback;
        }
    }
    // Durable XDG appearance when session store is empty (slideshow may paint a
    // path before installDisplayPixels seeds ItemWorld sparse tables for that id).
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
            item->setSessionId(sid);
        }
        WorkspaceItemState slot = freezeItemAppearance(item);
        slot.sessionId = sid;
        slot.sessionIndex = sessionListIndex(item);
        slot.path = item->path();
        // Full freeze replace into sparse tables (placement preserved when
        // identity — tip 2059).
        m_itemWorld.setAppearance(sid, slot);
        // Sparse Color is store authority for durable grade fields.
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
                || item->tileContentXform().hasCrop;
            emit sessionCropApplied(sid, item->path(), appearance, hasCrop);
        }
    }
}

// --- Colour grade (was imageview_color_grade.cpp) ---

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
        syncLiveColorFromState(item, want.colorAdjust, true);
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
        syncLiveColorFromState(item, want.colorAdjust);
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
        syncLiveColorFromState(item, want.colorAdjust);
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
    ImageItem *item = (sid != kInvalidSessionImageId)
        ? findItemBySessionId(sid)
        : nullptr;
    if (!item && isImageMode() && !m_items.isEmpty()) {
        item = m_items.first();
    }
    if (!item) {
        return;
    }
    // Crop draft freezes pixels; put the commit back so idle debounce retries
    // after leaveCrop (take already cleared the bag).
    if (m_cropCtrl.isCropDraftLockedItem(item)) {
        scheduleColorAdjustCommit(sid, path.isEmpty() ? item->path() : path);
        return;
    }
    // Stage 2: freeze policy (store + live when durable; else captureState).
    WorkspaceItemState want = freezeItemAppearance(item);
    // Flush always prefers live grade (interaction authority; ItemWorld Color
    // is already updated on setTargetColorAdjustments).
    want.colorAdjust = item->colorAdjustments();
    // Full rematerialize from host (async when multi-MP). Do **not** write
    // grade into path-keyed XDG on every slider tick — ItemWorld Color + project
    // own durable grade; XDG is for orient/flip seed, not slider spam.
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
    // Stage 2: freeze policy for grade slot seed.
    WorkspaceItemState slot = freezeItemAppearance(item);
    slot.sessionId = (sid != kInvalidSessionImageId) ? sid : slot.sessionId;
    slot.path = item->path().isEmpty() ? slot.path : item->path();
    slot.colorAdjust = adj;
    if (sid != kInvalidSessionImageId) {
        // ItemWorld Color is persistence authority (sparse-only). Live grade is
        // installed below via applyInteractiveColorGrade → syncLiveColorFromState.
        ItemComponents::Color c;
        c.grade = adj;
        m_itemWorld.setColor(sid, c);
    }
    // Fast path while dragging: live grade + optional host bake (no SQLite).
    applyInteractiveColorGrade(item, slot);
    if (sid != kInvalidSessionImageId || !item->path().isEmpty()) {
        scheduleColorAdjustCommit(sid, item->path());
    }
    emit statusChanged();
}

// --- Crop appearance (was imageview_crop.cpp) ---

void ImageView::storeCropAppearance(ImageItem *item, SessionImageId sid,
                                    const WorkspaceItemState &s)
{
    if (!item) {
        return;
    }
    if (sid != kInvalidSessionImageId) {
        // Crop commit owns crop + content bake only. Do not setAppearance the
        // full DTO — that would re-sync attention/color/placement from freeze
        // and risk clearing components not in this write path.
        m_itemWorld.setCrop(sid, ItemComponents::cropFromState(s));
        m_itemWorld.setContentBake(sid, ItemComponents::contentBakeFromState(s));
    } else {
        // Unbound only: path map is the sole store.
        m_itemWorld.setPathState(item->path(), s);
    }
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
    // Stage 2 / 4a: durable crop from sparse-prefer store, not a parallel
    // captureState rebuild. Live applied xform still uses captureState.
    if (sid != kInvalidSessionImageId && m_itemWorld.hasCrop(sid)) {
        *app = sessionAppearanceValue(sid);
        return true;
    }
    if (item->tileContentXform().hasCrop) {
        *app = freezeItemAppearance(item);
        return true;
    }
    // Unbound path map may hold crop; bound crop is sparse only.
    if (sid == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_itemWorld.getPathState(item->path())) {
            *app = *st;
            return true;
        }
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
    const QImage full = m_displayPipeline.fullRasterForEdit(path);
    if (!full.isNull() && !path.isEmpty()) {
        ImageCache::put(path, full);
    }
    CropSession::applyItemPlacementFromState(item, app, isImageMode());
    if (full.isNull()) {
        rematerializeItemContent(item, app);
    } else if (!tryRematerializeFromHost(item, app)) {
        m_displayPipeline.installDisplayPixels(item, full, SessionAppearance::PixelKind::FullSource, sid);
        if (!ContentXform::equal(item->tileContentXform(),
                                 ContentXform::Value::fromState(app))) {
            rematerializeItemContent(item, app);
        }
    }
    m_cropCtrl.fitImageOrUpdateWorkspace(item);
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
        syncLiveContentMetaFromState(item, state);
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
    // Appearance persistence is commitItemSessionEdit → ItemWorld sparse tables (by id).
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
