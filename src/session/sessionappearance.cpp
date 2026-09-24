// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "session/sessionappearance.h"
#include <QPointF>
#include "content/contentxform.h"
#include "crop/cropgeometry.h"
#include "view/viewtransform.h"
#include "util/biltoo_thread.h"
#include "color/coloradjust.h"
#include "host/thumtoocache.h"

#include <QtMath>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <QPolygonF>

namespace SessionAppearance {

QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live)
{
    return ContentXform::scaleCropRect(crop, recorded, live);
}

static void normalizeCropRotation(qreal &degrees)
{
    while (degrees > 180.0) {
        degrees -= 360.0;
    }
    while (degrees <= -180.0) {
        degrees += 360.0;
    }
}

void mapCropThroughContentFlip(WorkspaceItemState &state, bool horizontal, bool vertical)
{
    if (!state.hasCrop || state.cropRect.isEmpty() || (!horizontal && !vertical)) {
        return;
    }
    QSize sz = state.cropSourceSize;
    if (!sz.isValid() || sz.width() < 1 || sz.height() < 1) {
        // Last resort: treat the crop AABB as living in a canvas that just fits it.
        sz = QSize(state.cropRect.x() + state.cropRect.width(),
                   state.cropRect.y() + state.cropRect.height());
    }
    QRect r = state.cropRect.normalized();
    if (horizontal) {
        r = QRect(sz.width() - r.x() - r.width(), r.y(), r.width(), r.height());
        state.cropRotation = -state.cropRotation;
    }
    if (vertical) {
        r = QRect(r.x(), sz.height() - r.y() - r.height(), r.width(), r.height());
        state.cropRotation = -state.cropRotation;
    }
    normalizeCropRotation(state.cropRotation);
    state.cropRect = r;
}

void mapCropThroughContentRotate90(WorkspaceItemState &state, int quarterTurns)
{
    // Pure geometry lives in ContentXform (same matrix as QImage::trueMatrix).
    ContentXform::Value x = ContentXform::Value::fromState(state);
    ContentXform::mapCropThroughContentRotate90(x, quarterTurns);
    state.hasCrop = x.hasCrop;
    state.cropRect = x.cropRect;
    state.cropSourceSize = x.cropSourceSize;
    state.cropRotation = x.cropRotation;
}

QRectF mapSourceRectToContentDisplay(const QRectF &sourceRect, const QSize &sourceSize,
                                     const WorkspaceItemState &state)
{
    // Spaces: docs/CONTENT_COORDINATES.md
    // Order matches ImageItem bake path: flip pixels, then QImage::transformed
    // with QTransform::rotate(90 * turns). Crop is post-bake (display space).
    if (sourceSize.width() < 1 || sourceSize.height() < 1 || sourceRect.isEmpty()) {
        return {};
    }

    QRectF r = sourceRect.normalized();
    QSize work = sourceSize;

    // 1) Content flips (same as QImage::flipped before rotate).
    if (state.contentHFlip) {
        r = QRectF(qreal(work.width()) - r.x() - r.width(), r.y(), r.width(), r.height());
    }
    if (state.contentVFlip) {
        r = QRectF(r.x(), qreal(work.height()) - r.y() - r.height(), r.width(), r.height());
    }

    // 2) Quarter-turns: EXACT transform QImage uses (trueMatrix + rotate).
    const int turns = ContentXform::normalizeQuarterTurns(state.contentQuarterTurns);
    if (turns != 0) {
        QTransform rot;
        rot.rotate(90.0 * turns);
        const QTransform mat = QImage::trueMatrix(rot, work.width(), work.height());
        r = mat.mapRect(r).normalized();
        if ((turns % 2) != 0) {
            work = QSize(work.height(), work.width());
        }
    }

    // 3) Crop in post-orientation space.
    if (state.hasCrop && !state.cropRect.isEmpty()) {
        QRect crop = state.cropRect.normalized();
        QSize basis = state.cropSourceSize;
        if (basis.width() < 1 || basis.height() < 1) {
            basis = work;
        }
        if (basis != work) {
            crop = scaleCropRect(crop, basis, work);
            if (crop.right() >= work.width() || crop.bottom() >= work.height()) {
                const QSize swapped(basis.height(), basis.width());
                if (swapped != basis && swapped.width() > 0 && swapped.height() > 0) {
                    const QRect alt = scaleCropRect(state.cropRect.normalized(), swapped, work);
                    if (alt.right() < work.width() && alt.bottom() < work.height()
                        && alt.width() >= 1 && alt.height() >= 1) {
                        crop = alt;
                    }
                }
            }
        }
        if (crop.width() < 1 || crop.height() < 1) {
            return {};
        }
        r = r.intersected(QRectF(crop));
        if (r.isEmpty()) {
            return {};
        }
        r = r.translated(-qreal(crop.x()), -qreal(crop.y()));
    }

    return r;
}

bool hasContentAppearance(const WorkspaceItemState &state)
{
    return state.hasCrop || state.contentHFlip || state.contentVFlip
           || state.contentQuarterTurns != 0 || !state.colorAdjust.isIdentity();
}


bool liveItemHasContentMods(const ContentXform::Value &live)
{
    return live.hasCrop || live.hFlip || live.vFlip;
}

QImage materializeDisplay(const QImage &raw, const WorkspaceItemState &state,
                          PixelKind kind)
{
    if (raw.isNull()) {
        return {};
    }
    // No-op path is cheap and GUI-safe (Gallery ladderReady often lands here).
    if (!hasContentAppearance(state) && state.colorAdjust.isIdentity()) {
        return raw;
    }
    // Real bake of multi-MP must not run on the GUI thread.
    if (qMax(raw.width(), raw.height()) > ContentXform::kGuiMaterializeMaxEdge) {
        ASSERT_NOT_GUI_THREAD();
    }

    QImage out = raw;

    // 1) Content flips (source space), same as ImageItem::bakeFlip axes.
    if (state.contentHFlip || state.contentVFlip) {
        Qt::Orientations axes;
        if (state.contentHFlip) {
            axes |= Qt::Horizontal;
        }
        if (state.contentVFlip) {
            axes |= Qt::Vertical;
        }
        if (axes) {
            out = out.flipped(axes);
        }
    }

    // 2) Quarter-turns — same matrix as bakeRotate90 / QImage::transformed.
    int turns = state.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    if (turns != 0) {
        QTransform rot;
        rot.rotate(90.0 * turns);
        // Soft stand-ins must stay cheap on the GUI thread; Smooth on multi-MP
        // FullSource is reserved for final installs.
        const Qt::TransformationMode mode =
            (kind == PixelKind::SoftPreview) ? Qt::FastTransformation
                                             : Qt::SmoothTransformation;
        out = out.transformed(rot, mode);
    }

    // 3) Crop in *post-orient* space (cropRect after mapCropThrough*).
    // Free rotation uses the same window sample as ImageItem::materializeDisplay.
    if (state.hasCrop && !state.cropRect.isEmpty()) {
        const QSize live = out.size();
        QRect crop = scaleCropRect(state.cropRect, state.cropSourceSize, live);
        if (state.cropSourceSize.isEmpty()
            && (crop.right() >= live.width() || crop.bottom() >= live.height())) {
            const QSize swapped(live.height(), live.width());
            if (swapped.width() > 0 && swapped.height() > 0
                && crop.right() < swapped.width() && crop.bottom() < swapped.height()
                && swapped != live) {
                crop = scaleCropRect(state.cropRect, swapped, live);
            }
        }
        if (crop.width() >= 1 && crop.height() >= 1) {
            const bool freeRot = qAbs(state.cropRotation) > CropGeometry::kFreeRotationEps;
            if (!freeRot) {
                const QRect bounds(0, 0, out.width(), out.height());
                const QRect srcRect = crop.intersected(bounds);
                if (srcRect.width() >= 1 && srcRect.height() >= 1) {
                    out = out.copy(srcRect);
                }
            } else {
                const int dw = ViewTransform::atLeast1(crop.width());
                const int dh = ViewTransform::atLeast1(crop.height());
                QImage::Format fmt = out.format();
                if (fmt == QImage::Format_Invalid) {
                    fmt = QImage::Format_ARGB32_Premultiplied;
                }
                QImage cropped(dw, dh, fmt);
                if (!cropped.isNull()) {
                    cropped.fill(Qt::transparent);
                    QPainter painter(&cropped);
                    painter.setRenderHint(QPainter::SmoothPixmapTransform,
                                         kind != PixelKind::SoftPreview);
                    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                    const QPointF srcCenter(crop.center());
                    painter.translate(dw / 2.0, dh / 2.0);
                    painter.rotate(-state.cropRotation);
                    painter.translate(-srcCenter.x(), -srcCenter.y());
                    painter.drawImage(0, 0, out);
                    painter.end();
                    out = cropped;
                }
            }
        }
    }

    // 4) Colour grade baked into returned pixels for soft/blit paths.
    if (!state.colorAdjust.isIdentity()) {
        out = applyColorAdjustments(out, state.colorAdjust);
    }

    // Assert: crop want must not produce empty/1×1 when the crop rect is larger.
    if (state.hasCrop && !state.cropRect.isEmpty() && !out.isNull()) {
        const QRect want = state.cropRect.normalized();
        if (want.width() > 2 && want.height() > 2
            && (out.width() <= 1 || out.height() <= 1)) {
            qCritical("materializeDisplay: crop %dx%d @ source %dx%d → output %dx%d "
                      "(cropSourceSize %dx%d raw was likely wrong space)",
                      want.width(), want.height(),
                      state.cropSourceSize.width(), state.cropSourceSize.height(),
                      out.width(), out.height(),
                      state.cropSourceSize.width(), state.cropSourceSize.height());
        }
    }
    return out;
}

QImage applyContentToImage(const QImage &src, const WorkspaceItemState &state,
                           PixelKind kind)
{
    return materializeDisplay(src, state, kind);
}


void fillEmptyContentFlags(WorkspaceItemState &appearance,
                           const ContentXform::Value &flags)
{
    // Fill empty store fields only (OR-merge; never clear store).
    if (flags.quarterTurns != 0 && appearance.contentQuarterTurns == 0) {
        appearance.contentQuarterTurns =
            ContentXform::normalizeQuarterTurns(flags.quarterTurns);
    }
    if (flags.hFlip) {
        appearance.contentHFlip = true;
    }
    if (flags.vFlip) {
        appearance.contentVFlip = true;
    }
    if (flags.hasCrop && appearance.cropRect.isEmpty()) {
        appearance.hasCrop = true;
        appearance.cropRect = flags.cropRect;
    }
}


WorkspaceItemState withoutCrop(const WorkspaceItemState &state)
{
    WorkspaceItemState out = state;
    out.hasCrop = false;
    out.cropRect = QRect();
    out.cropSourceSize = QSize();
    out.cropRotation = 0.0;
    return out;
}


WorkspaceItemState withoutContentOrient(const WorkspaceItemState &state)
{
    WorkspaceItemState out = withoutCrop(state);
    out.contentHFlip = false;
    out.contentVFlip = false;
    out.contentQuarterTurns = 0;
    return out;
}


WorkspaceItemState orientAuthorityWant(bool hasContentOrient,
                                       const WorkspaceItemState &want)
{
    if (hasContentOrient) {
        return want;
    }
    return withoutContentOrient(want);
}

bool wantSpecifiesContentOrient(const WorkspaceItemState &want)
{
    return want.hasCrop || want.contentHFlip || want.contentVFlip
           || ContentXform::normalizeQuarterTurns(want.contentQuarterTurns) != 0;
}


WorkspaceItemState clearedContentOps(const WorkspaceItemState &state)
{
    WorkspaceItemState out = withoutContentOrient(state);
    // Colour grade is part of content appearance (hasContentAppearance /
    // Reset Content Appearance help text).
    out.colorAdjust = {};
    return out;
}


void applyStoredContentAppearance(WorkspaceItemState *st,
                                  const ThumtooCache::StoredContentAppearance &stored,
                                  bool includeGrade, bool includeCrop)
{
    if (!st) {
        return;
    }
    st->contentHFlip = stored.contentHFlip;
    st->contentVFlip = stored.contentVFlip;
    st->contentQuarterTurns = stored.contentQuarterTurns;
    if (includeCrop) {
        if (stored.hasCrop) {
            st->hasCrop = true;
            st->cropRect = stored.cropRect;
            st->cropSourceSize = stored.cropSourceSize;
            st->cropRotation = stored.cropRotation;
        } else {
            st->hasCrop = false;
            st->cropRect = QRect();
            st->cropSourceSize = QSize();
            st->cropRotation = 0.0;
        }
    }
    if (includeGrade && stored.hasGrade) {
        st->colorAdjust = ColorAdjustments::fromDurableGrade(
            stored.gradeBrightness, stored.gradeContrast,
            stored.gradeSaturation, stored.gradeHue,
            stored.gradeGamma, stored.gradeInvert);
    }
}


bool fillStoredContentAppearance(ThumtooCache::StoredContentAppearance *stored,
                                 const WorkspaceItemState &state,
                                 bool writeCrop)
{
    if (!stored) {
        return false;
    }
    *stored = {};
    stored->contentHFlip = state.contentHFlip;
    stored->contentVFlip = state.contentVFlip;
    stored->contentQuarterTurns = state.contentQuarterTurns;
    const bool cropOut = writeCrop && state.hasCrop && !state.cropRect.isEmpty();
    stored->hasCrop = cropOut;
    if (cropOut) {
        stored->cropRect = state.cropRect;
        stored->cropSourceSize = state.cropSourceSize;
        stored->cropRotation = state.cropRotation;
    }
    if (!state.colorAdjust.isIdentity()) {
        stored->hasGrade = true;
        stored->gradeBrightness = state.colorAdjust.brightness;
        stored->gradeContrast = state.colorAdjust.contrast;
        stored->gradeSaturation = state.colorAdjust.saturation;
        stored->gradeHue = state.colorAdjust.hue;
        stored->gradeGamma = ColorAdjustments::gammaToPercent(state.colorAdjust.gamma);
        stored->gradeInvert = state.colorAdjust.invert;
    }
    return !stored->isIdentity();
}


WorkspaceItemState appearanceCopyWithIdentityPose(const WorkspaceItemState &src,
                                                   SessionImageId toId)
{
    WorkspaceItemState dst = src;
    dst.sessionId = toId;
    dst.pos = QPointF();
    dst.scale = 1.0;
    dst.scaleY = 1.0;
    dst.shear = 0.0;
    dst.rotation = 0.0;
    dst.opacity = 1.0;
    dst.z = 0.0;
    return dst;
}


void fillUnboundContentFromLiveAndPath(WorkspaceItemState &s,
                                       const ContentXform::Value &live,
                                       const WorkspaceItemState *pathPrev)
{
    s.hasCrop = live.hasCrop;
    s.cropRect = live.cropRect;
    s.contentHFlip = live.hFlip;
    s.contentVFlip = live.vFlip;
    if (pathPrev) {
        s.contentQuarterTurns =
            ContentXform::normalizeQuarterTurns(pathPrev->contentQuarterTurns);
        s.cropRotation = pathPrev->cropRotation;
        s.cropSourceSize = pathPrev->cropSourceSize;
        if (pathPrev->hasCrop && !s.hasCrop) {
            s.hasCrop = true;
            s.cropRect = pathPrev->cropRect;
        }
    } else if (live.quarterTurns != 0) {
        s.contentQuarterTurns =
            ContentXform::normalizeQuarterTurns(live.quarterTurns);
    }
}

void overlayAppliedContentXform(WorkspaceItemState &s,
                                const ContentXform::Value &live)
{
    s.hasCrop = live.hasCrop;
    s.cropRect = live.cropRect;
    s.cropSourceSize = live.cropSourceSize;
    s.cropRotation = live.cropRotation;
    s.contentQuarterTurns =
        ContentXform::normalizeQuarterTurns(live.quarterTurns);
    s.contentHFlip = live.hFlip;
    s.contentVFlip = live.vFlip;
}

void adoptPathSessionIndexHint(WorkspaceItemState &s,
                               const WorkspaceItemState *pathPrev)
{
    if (s.sessionIndex >= 0 || !pathPrev) {
        return;
    }
    if (pathPrev->sessionIndex >= 0) {
        s.sessionIndex = pathPrev->sessionIndex;
    }
}

bool preferDurableFreeze(SessionImageId sid,
                         bool hasAppliedContentXform,
                         bool hasDurableAppearance)
{
    return sid != kInvalidSessionImageId
        && !hasAppliedContentXform
        && hasDurableAppearance;
}

WorkspaceItemState durableFreezeFromParts(
    const WorkspaceItemState &durableAppearance,
    const ItemComponents::Placement &livePlacement,
    const ColorAdjustments &liveColor,
    const QString &path,
    SessionImageId sid,
    int sessionIndex)
{
    WorkspaceItemState s = durableAppearance;
    ItemComponents::applyPlacementToState(s, livePlacement);
    s.colorAdjust = liveColor;
    s.path = path;
    s.sessionId = sid;
    s.sessionIndex = sessionIndex;
    return s;
}


WorkspaceItemState mergeAppliedIntoDurable(const WorkspaceItemState &durable,
                                           const ContentXform::Value &applied,
                                           SessionImageId sid,
                                           const QString &path)
{
    WorkspaceItemState s = durable;
    // applyToState overwrites every field. Applied is orient/crop mid-edit
    // fingerprint (and paint may mirror live lag into applied.colorAdjust via
    // attachDisplaySample). Durable Color is only written by the grade commit
    // path — never promote applied.colorAdjust.
    const ColorAdjustments durableColor = s.colorAdjust;
    const bool hadCrop = s.hasCrop;
    const QRect durableCropRect = s.cropRect;
    const QSize durableCropSource = s.cropSourceSize;
    const qreal durableCropRot = s.cropRotation;
    applied.applyToState(s);
    s.colorAdjust = durableColor;
    if (!applied.hasCrop && hadCrop) {
        s.hasCrop = true;
        s.cropRect = durableCropRect;
        s.cropSourceSize = durableCropSource;
        s.cropRotation = durableCropRot;
    }
    s.sessionId = sid;
    s.path = path;
    return s;
}

SessionImageId resolveEditSessionId(SessionImageId itemSid,
                                    bool imageMode,
                                    SessionImageId currentImageSid)
{
    if (itemSid != kInvalidSessionImageId) {
        return itemSid;
    }
    if (imageMode) {
        return currentImageSid;
    }
    return kInvalidSessionImageId;
}


RememberKind rememberKind(bool imageMode,
                          SessionImageId editSessionId,
                          SessionImageId itemSessionId)
{
    if (imageMode) {
        // Bound session images: appearance lives in ItemWorld sparse tables.
        // Leave path-map placement untouched; do not last-write crop/flip by path.
        if (editSessionId != kInvalidSessionImageId) {
            return RememberKind::Skip;
        }
        // Unbound legacy tile: path map is the only store.
        return RememberKind::WritePathFreeze;
    }
    // Workspace / Gallery: path map is legacy placement for *unbound* tiles only.
    // Bound: placement by id (never pose by path — duplicates would collide).
    if (itemSessionId != kInvalidSessionImageId) {
        return RememberKind::WritePlacementOnly;
    }
    return RememberKind::WritePathFreeze;
}


QImage applyLiveDisplayOverlays(QImage img, bool hFlip, bool vFlip,
                                bool hasAppliedContentXform,
                                const ColorAdjustments &liveColor)
{
    if (img.isNull()) {
        return img;
    }
    // Placement display flips (should be empty after content bake into pixels).
    if (hFlip || vFlip) {
        Qt::Orientations axes;
        if (hFlip) {
            axes |= Qt::Horizontal;
        }
        if (vFlip) {
            axes |= Qt::Vertical;
        }
        img = img.flipped(axes);
    }
    // Live grade only when display is still unbaked host (no applied xform).
    // Re-applying on a materialize bake double-grades the filmstrip override.
    if (!hasAppliedContentXform && !liveColor.isIdentity()) {
        img = applyColorAdjustments(img, liveColor);
    }
    return img;
}


WorkspaceItemState preferDurableColor(WorkspaceItemState slot,
                                      bool hasDurableColor,
                                      const ColorAdjustments &durableGrade)
{
    if (hasDurableColor) {
        slot.colorAdjust = durableGrade;
    }
    return slot;
}

bool shouldWriteCropToPathStore(bool sessionBound,
                                bool hasCrop,
                                bool cropRectEmpty)
{
    return !sessionBound && hasCrop && !cropRectEmpty;
}


bool assembleSoftPaintState(WorkspaceItemState *out,
                            const WorkspaceItemState *app,
                            SessionImageId sid,
                            bool hasSparseColor,
                            const ColorAdjustments &sparseGrade)
{
    if (!out) {
        return false;
    }
    // Prefer sparse Color grade for filmstrip / soft paint. Grade-only sparse
    // presence still materializes when only color (or other single component) is set.
    WorkspaceItemState paint;
    if (app) {
        paint = *app;
    } else if (sid == kInvalidSessionImageId || !hasSparseColor) {
        return false;
    }
    if (sid != kInvalidSessionImageId) {
        paint.colorAdjust = sparseGrade;
        paint.sessionId = sid;
    }
    if (!hasContentAppearance(paint) && paint.colorAdjust.isIdentity()) {
        return false;
    }
    *out = paint;
    return true;
}


QSize layoutSizeOrNative(const QSize &native, const WorkspaceItemState &want)
{
    const QSize lay = ContentXform::layoutSize(native, want);
    if (lay.width() > 1 && lay.height() > 1) {
        return lay;
    }
    return native;
}

bool galleryCellAspectStale(const QSizeF &cell, const QSize &layoutSize, qreal tolerance)
{
    if (cell.isEmpty() || layoutSize.width() <= 0 || layoutSize.height() <= 0) {
        return false;
    }
    if (!(cell.height() > 1e-3 && cell.width() > 1e-3)) {
        return false;
    }
    const qreal cellAr = cell.width() / cell.height();
    const qreal layAr = qreal(layoutSize.width()) / qreal(layoutSize.height());
    return qAbs(cellAr - layAr) > tolerance;
}


QSize pickNativeSize(const QSize &logical, const QSize &bookKnown,
                     const QSize &storeKnown, bool allowStore)
{
    const auto ok = [](const QSize &s) {
        return s.width() > 1 && s.height() > 1;
    };
    if (ok(logical)) {
        return logical;
    }
    if (ok(bookKnown)) {
        return bookKnown;
    }
    if (allowStore && ok(storeKnown)) {
        return storeKnown;
    }
    // Provisional: book (or empty logical) so callers can still schedule probes.
    return bookKnown.isValid() && !bookKnown.isEmpty() ? bookKnown : logical;
}

QSize resolveContentLayoutSize(const QSize &logical, const QSize &bookKnown,
                               const QSize &storeKnown, bool allowStoreAppearance,
                               SessionImageId sessionId, const QString &path,
                               bool hasBoundDurable,
                               const WorkspaceItemState *boundAppearance,
                               bool hasContentOrient,
                               const WorkspaceItemState *pathState)
{
    // Ground truth: native × content ops (same as filmstrip provider).
    const QSize native = pickNativeSize(logical, bookKnown, storeKnown, allowStoreAppearance);
    if (!(native.width() > 1 && native.height() > 1)) {
        // Callers still call layoutSizeForPath which schedules probes.
        return native; // may be empty/provisional
    }
    WorkspaceItemState want;
    if (hasBoundDurable && boundAppearance) {
        want = *boundAppearance;
        // Placement/color-only durable row is not content orient — same strip as
        // installDisplayPixels / createItemFromImage (2205–2211).
        want = orientAuthorityWant(hasContentOrient, want);
    } else if (sessionId == kInvalidSessionImageId && pathState) {
        want = *pathState;
    }
    // Bound: never path XDG. Unbound path rows may still use full XDG including crop.
    // Virtual plan / bulk open: never Store loadContentAppearance (GUI freeze).
    if (shouldAugmentFromPathStore(allowStoreAppearance, hasContentAppearance(want),
                                   path.isEmpty(), sessionId)) {
        ThumtooCache::StoredContentAppearance stored;
        if (ThumtooCache::loadContentAppearance(path, &stored) && !stored.isIdentity()) {
            applyStoredContentAppearance(&want, stored, false);
        }
    }
    return layoutSizeOrNative(native, want);
}

QImage softImageWithAppearanceSources(const QImage &src, SessionImageId sid,
                                      const QString &path,
                                      const WorkspaceItemState *boundApp,
                                      const WorkspaceItemState *pathState,
                                      bool hasSparseColor,
                                      const ColorAdjustments &sparseGrade)
{
    if (src.isNull()) {
        return {};
    }
    const WorkspaceItemState *app = boundApp;
    WorkspaceItemState fallback;
    // Unbound only: path map + path XDG. Bound content is ItemWorld sparse only
    // (matches Image underlay / contentLayoutSize — no path XDG for bound ids).
    if ((!app || !hasContentAppearance(*app))
        && sid == kInvalidSessionImageId && !path.isEmpty()) {
        if (pathState) {
            fallback = *pathState;
            app = &fallback;
        }
        if ((!app || !hasContentAppearance(*app))) {
            ThumtooCache::StoredContentAppearance stored;
            if (ThumtooCache::loadContentAppearance(path, &stored)
                && stored.hasOrientContent()) {
                fallback = {};
                fallback.path = path;
                applyStoredContentAppearance(&fallback, stored, false);
                app = &fallback;
            }
        }
    }
    WorkspaceItemState paint;
    if (!assembleSoftPaintState(
            &paint, app, sid, hasSparseColor,
            hasSparseColor ? sparseGrade : ColorAdjustments{})) {
        return src;
    }
    // Single pipeline — SoftPreview scales crop into soft pixel space
    // (materializeDisplay contract). Never strip crop.
    const QImage out = materializeDisplay(
        src, paint, PixelKind::SoftPreview);
    return out.isNull() ? src : out;
}



bool shouldAugmentFromPathStore(bool allowStoreAppearance,
                                bool hasContentAlready,
                                bool pathEmpty,
                                SessionImageId sid)
{
    return allowStoreAppearance && !hasContentAlready && !pathEmpty
        && sid == kInvalidSessionImageId;
}

bool itemShowsContentEdit(SessionImageId sid,
                          bool hasContentEditComponents,
                          bool hasDurableAppearance,
                          bool durableHasContentAppearance,
                          bool liveHasContentMods,
                          bool pathHasContentAppearance)
{
    if (sid != kInvalidSessionImageId) {
        if (hasContentEditComponents) {
            return true;
        }
        if (hasDurableAppearance && durableHasContentAppearance) {
            return true;
        }
    }
    if (liveHasContentMods) {
        return true;
    }
    return pathHasContentAppearance;
}


CropRestoreSource cropRestoreSource(bool sessionStoreLoaded,
                                    SessionImageId sid,
                                    bool hasSparseCrop,
                                    bool liveAppliedHasCrop,
                                    bool hasPathState)
{
    if (sessionStoreLoaded) {
        return CropRestoreSource::SessionStore;
    }
    if (sid != kInvalidSessionImageId && hasSparseCrop) {
        return CropRestoreSource::SparseCrop;
    }
    if (liveAppliedHasCrop) {
        return CropRestoreSource::LiveAppliedFreeze;
    }
    if (sid == kInvalidSessionImageId && hasPathState) {
        return CropRestoreSource::PathMap;
    }
    return CropRestoreSource::None;
}


ColorAdjustments preferLiveColor(bool hasLiveColorLag,
                                 const ColorAdjustments &lag,
                                 const ColorAdjustments &itemGrade)
{
    if (hasLiveColorLag) {
        return lag;
    }
    return itemGrade;
}

BindLagAction bindLiveColorLagAction(bool hasDurableColor,
                                     bool itemGradeIsIdentity)
{
    if (hasDurableColor) {
        return BindLagAction::FromDurableColor;
    }
    if (!itemGradeIsIdentity) {
        return BindLagAction::FromItemGrade;
    }
    return BindLagAction::None;
}

} // namespace SessionAppearance
