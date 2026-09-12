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

QImage materializeDisplay(const QImage &raw, const WorkspaceItemState &state,
                          PixelKind kind)
{
    if (raw.isNull()) {
        return {};
    }
    if (!hasContentAppearance(state) && state.colorAdjust.isIdentity()) {
        return raw;
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
        out = out.transformed(rot, Qt::SmoothTransformation);
    }

    // 3) Crop in *post-orient* space (cropRect after mapCropThrough*).
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
            const QRect bounds(0, 0, out.width(), out.height());
            const QRect srcRect = crop.intersected(bounds);
            if (srcRect.width() >= 1 && srcRect.height() >= 1) {
                out = out.copy(srcRect);
            }
        }
        Q_UNUSED(kind);
    }

    // 4) Colour grade baked into returned pixels for soft/blit paths.
    if (!state.colorAdjust.isIdentity()) {
        out = applyColorAdjustments(out, state.colorAdjust);
    }
    return out;
}

void applyContentToItem(ImageItem *item, const WorkspaceItemState &state)
{
    if (!item) {
        return;
    }
    // Contract: @p item holds *raw* full pixels. Sole bake for install from disk.
    // Live bakeFlip/bakeRotate90 mutate display pixels and update session state.
    QImage raw = item->sourceImage();
    if (raw.isNull()) {
        raw = item->previewImage();
    }
    if (raw.isNull()) {
        return;
    }

    if (state.hasCrop && !state.cropRect.isEmpty()
        && qAbs(state.cropRotation) > 0.05) {
        WorkspaceItemState orientOnly = state;
        orientOnly.hasCrop = false;
        item->setSourceImage(materializeDisplay(raw, orientOnly, PixelKind::FullSource));
        applyCrop(item, state);
    } else {
        item->setSourceImage(materializeDisplay(raw, state, PixelKind::FullSource));
    }

    item->setContentHFlip(state.contentHFlip);
    item->setContentVFlip(state.contentVFlip);
    item->setSessionCrop(state.hasCrop, state.cropRect);
    item->setColorAdjustments(state.colorAdjust);
    syncItemLayoutToContentOrientation(item, state);
}

void syncItemLayoutToContentOrientation(ImageItem *item,
                                        const WorkspaceItemState &state)
{
    Q_UNUSED(state);
    if (!item) {
        return;
    }
    // Logical size owns geometry. Soft samples may inform *aspect* only.
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

    const bool haveFull = item->hasDecodedPixels();

    if (layout.width() < 1 || layout.height() < 1) {
        // Layout still unknown: only full (non-preview) pixels may seed
        // magnitude. Soft must not define geometry.
        if (haveFull) {
            item->setIntrinsicSize(display);
        }
        return;
    }

    const bool displayLandscape = display.width() >= display.height();
    const bool layoutLandscape = layout.width() >= layout.height();
    if (displayLandscape == layoutLandscape) {
        // Matching aspect. Grow magnitude only from true full pixels that are
        // larger than the current logical size (never soft / smaller ladder).
        if (haveFull) {
            const qint64 have =
                qint64(layout.width()) * qint64(layout.height());
            const qint64 incoming =
                qint64(display.width()) * qint64(display.height());
            if (incoming > have) {
                item->setIntrinsicSize(display);
            }
        }
        return;
    }

    // Aspect mismatch (content orientation): transpose layout magnitude for
    // soft; adopt oriented full pixels only when they are full decode.
    if (haveFull) {
        item->setIntrinsicSize(display);
    } else {
        item->setIntrinsicSize(QSize(layout.height(), layout.width()));
    }
}

QImage applyContentToImage(const QImage &src, const WorkspaceItemState &state,
                           PixelKind kind)
{
    return materializeDisplay(src, state, kind);
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
