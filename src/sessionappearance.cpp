// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "imageitem.h"
#include "coloradjust.h"

#include <QtMath>

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
    for (int i = 0; i < quarterTurns; ++i) {
        // 90° CW in top-left image coordinates (matches QImage bakeRotate90).
        r = QRect(sz.height() - r.y() - r.height(), r.x(), r.height(), r.width());
        sz = QSize(sz.height(), sz.width());
        state.cropRotation -= 90.0;
    }
    normalizeCropRotation(state.cropRotation);
    state.cropRect = r;
    state.cropSourceSize = sz;
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
    if (!item) {
        return;
    }
    // Oriented display pixels (full source or soft preview). setPreviewImage
    // clears the QPixmap — never use pixmap() here or soft path falls through
    // to a blind transpose that toggles aspect on every reinstall (focus/click).
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
        // Soft magnitude is not native — only seed aspect family via display
        // when layout is still unknown; full source may adopt pixel size.
        if (!item->sourceImage().isNull()) {
            item->setIntrinsicSize(display);
        } else {
            item->setIntrinsicSize(display);
        }
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
