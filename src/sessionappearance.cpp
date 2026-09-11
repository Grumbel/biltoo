// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "imageitem.h"
#include "coloradjust.h"

#include <QtMath>
#include <QImage>
#include <QTransform>
#include <QPolygonF>

namespace SessionAppearance {

QRect scaleCropRect(const QRect &crop, const QSize &recorded, const QSize &live)
{
    if (crop.isEmpty() || live.width() < 1 || live.height() < 1) {
        return {};
    }
    if (!recorded.isValid() || recorded.width() < 1 || recorded.height() < 1
        || recorded == live) {
        return crop;
    }
    return QRect(
        qRound(crop.x() * double(live.width()) / double(recorded.width())),
        qRound(crop.y() * double(live.height()) / double(recorded.height())),
        qMax(1, qRound(crop.width() * double(live.width()) / double(recorded.width()))),
        qMax(1, qRound(crop.height() * double(live.height()) / double(recorded.height()))));
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
    if (!state.hasCrop || state.cropRect.isEmpty() || quarterTurns == 0) {
        return;
    }
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    if (quarterTurns == 0) {
        return;
    }
    QSize sz = state.cropSourceSize;
    if (!sz.isValid() || sz.width() < 1 || sz.height() < 1) {
        sz = QSize(state.cropRect.x() + state.cropRect.width(),
                   state.cropRect.y() + state.cropRect.height());
    }
    QRect r = state.cropRect.normalized();
    // Same matrix as bakeRotate90 / mapSourceRectToContentDisplay (absolute steps).
    QTransform rot;
    rot.rotate(90.0 * quarterTurns);
    const QTransform mat = QImage::trueMatrix(rot, sz.width(), sz.height());
    r = mat.mapRect(QRectF(r)).toRect().normalized();
    if (r.width() < 1) {
        r.setWidth(1);
    }
    if (r.height() < 1) {
        r.setHeight(1);
    }
    if ((quarterTurns % 2) != 0) {
        sz = QSize(sz.height(), sz.width());
    }
    state.cropRotation -= 90.0 * quarterTurns;
    normalizeCropRotation(state.cropRotation);
    state.cropRect = r;
    state.cropSourceSize = sz;
}

static int normalizeQuarterTurns(int quarterTurns)
{
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    return quarterTurns;
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
    //    Do not hand-roll CW/CCW formulas — they drift from Qt's adjusted matrix.
    const int turns = normalizeQuarterTurns(state.contentQuarterTurns);
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

void applyCrop(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item || !state.hasCrop || state.cropRect.isEmpty()) {
        return;
    }
    const QSize sz = item->imageSize();
    if (sz.width() < 1 || sz.height() < 1) {
        return;
    }
    QRect crop = scaleCropRect(state.cropRect, state.cropSourceSize, sz);
    // Legacy: rect only fits orientation-swapped dimensions.
    if (state.cropSourceSize.isEmpty()
        && (crop.right() >= sz.width() || crop.bottom() >= sz.height())) {
        const QSize swapped(sz.height(), sz.width());
        if (swapped.width() > 0 && swapped.height() > 0
            && crop.right() < swapped.width() && crop.bottom() < swapped.height()
            && swapped != sz) {
            crop = scaleCropRect(state.cropRect, swapped, sz);
        }
    }
    if (crop.width() < 1 || crop.height() < 1) {
        return;
    }
    const QPointF off = item->offset();
    // May extend outside the source; cropToLocalRect pads as needed.
    const QRectF local(crop.x() + off.x(), crop.y() + off.y(),
                       crop.width(), crop.height());
    item->cropToLocalRect(local, QColor(0, 0, 0, 0), state.cropRotation);
}

bool hasContentAppearance(const WorkspaceItemState &state)
{
    return state.hasCrop || state.contentHFlip || state.contentVFlip
           || state.contentQuarterTurns != 0 || !state.colorAdjust.isIdentity();
}

bool contentSwapsAspect(const WorkspaceItemState &state)
{
    int turns = state.contentQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    return turns == 1 || turns == 3;
}

void applyContentToItem(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Contract: for crop / content flip / quarter-turns, @p item must hold the
    // full on-disk pixels (not a previously baked result). Callers that may
    // re-apply (applyStoredAppearance, workspace restore) reload first.
    // Colour grade alone is non-destructive and safe on any current source.
    // 1) Crop from current source.
    applyCrop(item, state);
    // 2) Content bakes — order matches live bakeFlip / bakeRotate90.
    if (state.contentHFlip || state.contentVFlip) {
        item->bakeFlip(state.contentHFlip, state.contentVFlip);
    }
    if (state.contentQuarterTurns != 0) {
        item->bakeRotate90(state.contentQuarterTurns);
    }
    // 3) Chrome / session meta for crop + content orientation.
    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
    item->setSessionCrop(state.hasCrop, state.cropRect);
    // 4) Non-destructive colour grade (display path in ImageItem).
    item->setColorAdjustments(state.colorAdjust);
    // 5) Layout geometry must match content orientation (all view modes).
    syncItemLayoutToContentOrientation(item, state);
}

void syncItemLayoutToContentOrientation(ImageItem *item,
                                        const WorkspaceItemState &state)
{
    Q_UNUSED(state);
    if (!item) {
        return;
    }
    // Oriented display pixels (full source or soft preview). setPreviewImage
    // clears the QPixmap — never use pixmap() here or soft path falls through
    // to a blind transpose that toggles aspect on every reinstall (focus/click).
    // `state` is kept in the signature for call-site symmetry with apply paths;
    // layout follows installed pixels, not a second decode of appearance flags.
    QSize display = item->sourceImage().size();
    if (display.width() < 1 || display.height() < 1) {
        display = item->previewImage().size();
    }

    const QSize layout = item->imageSize();
    if (display.width() < 1 || display.height() < 1) {
        // No pixels yet: do not guess. Blind transpose is not idempotent and
        // corrupts an already-oriented cell when soft/probe runs again.
        return;
    }

    if (layout.width() < 1 || layout.height() < 1) {
        // Layout still unknown: seed aspect/magnitude from whatever pixels we
        // have (full source or soft preview).
        item->setIntrinsicSize(display);
        return;
    }

    const bool displayLandscape = display.width() >= display.height();
    const bool layoutLandscape = layout.width() >= layout.height();
    if (displayLandscape == layoutLandscape) {
        // Already matching. Full decode may still promote magnitude.
        if (!item->sourceImage().isNull()
            && display.width() * display.height()
                >= layout.width() * layout.height()) {
            item->setIntrinsicSize(display);
        }
        return;
    }

    // Aspect mismatch: align layout to oriented display without adopting soft
    // ladder magnitude (keeps probe/native scale used at pack time).
    if (!item->sourceImage().isNull()) {
        item->setIntrinsicSize(display);
    } else {
        item->setIntrinsicSize(QSize(layout.height(), layout.width()));
    }
}

QImage applyContentToImage(const QImage &src, const WorkspaceItemState &state,
                           PixelKind kind)
{
    if (src.isNull() || !hasContentAppearance(state)) {
        return src;
    }

    QImage out = src;

    // 1) Crop (scaled into soft pixel space when SoftPreview).
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
            // Soft path: approximate axis-aligned crop only (no pad / fine rotation).
            // FullSource path prefers applyContentToItem + cropToLocalRect when
            // cropRotation is non-zero; here we still crop the AABB for blits.
            const QRect bounds(0, 0, out.width(), out.height());
            const QRect srcRect = crop.intersected(bounds);
            if (srcRect.width() >= 1 && srcRect.height() >= 1) {
                out = out.copy(srcRect);
            }
        }
        Q_UNUSED(kind);
    }

    // 2) Content flips then quarter turns (same order as bakeFlip / bakeRotate90).
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
    if (state.contentQuarterTurns != 0) {
        int turns = state.contentQuarterTurns % 4;
        if (turns < 0) {
            turns += 4;
        }
        if (turns != 0) {
            QTransform xform;
            xform.rotate(90.0 * turns);
            out = out.transformed(xform, Qt::SmoothTransformation);
        }
    }

    // 3) Colour grade (baked into the returned image for blits / soft stand-ins).
    if (!state.colorAdjust.isIdentity()) {
        out = applyColorAdjustments(out, state.colorAdjust);
    }

    return out;
}

} // namespace SessionAppearance

const WorkspaceItemState *SessionAppearanceStore::get(SessionImageId id) const
{
    if (id == kInvalidSessionImageId) {
        return nullptr;
    }
    const auto it = m_byId.constFind(id);
    if (it == m_byId.cend()) {
        return nullptr;
    }
    return &(*it);
}

WorkspaceItemState SessionAppearanceStore::value(SessionImageId id) const
{
    if (const WorkspaceItemState *p = get(id)) {
        return *p;
    }
    return {};
}

bool SessionAppearanceStore::contains(SessionImageId id) const
{
    return get(id) != nullptr;
}

void SessionAppearanceStore::set(SessionImageId id, const WorkspaceItemState &state)
{
    if (id == kInvalidSessionImageId) {
        return;
    }
    WorkspaceItemState s = state;
    s.sessionId = id;
    m_byId.insert(id, s);
}

void SessionAppearanceStore::remove(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_byId.remove(id);
    }
}

void SessionAppearanceStore::clear()
{
    m_byId.clear();
}
