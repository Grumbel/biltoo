// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "contentxform.h"
#include "biltoo_thread.h"
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

int normalizeQuarterTurns(int quarterTurns)
{
    return ContentXform::normalizeQuarterTurns(quarterTurns);
}

bool contentSwapsAspect(const ContentXform::Value &x)
{
    return ContentXform::swapsAspect(x);
}

bool contentSwapsAspect(const WorkspaceItemState &state)
{
    return ContentXform::swapsAspect(ContentXform::Value::fromState(state));
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
    // Prefer ImageView::rematerializeItemContent (host cache + async multi-MP).
    // This helper only runs GUI-safe materialize (≤ kGuiMaterializeMaxEdge).
    // Contract: @p item holds *raw* pixels (or soft). Already-baked display must
    // not be passed here or transforms double-apply.
    QImage raw = item->sourceImage();
    if (raw.isNull()) {
        raw = item->previewImage();
    }
    if (raw.isNull()) {
        return;
    }
    const int edge = qMax(raw.width(), raw.height());
    if (edge > ContentXform::kGuiMaterializeMaxEdge
        && (hasContentAppearance(state) || !state.colorAdjust.isIdentity())) {
        // Cannot materialize multi-MP on GUI — chrome only; caller should schedule
        // ImageView::rematerializeItemContent / scheduleAsyncHostRematerialize.
        item->setContentHFlip(state.contentHFlip);
        item->setContentVFlip(state.contentVFlip);
        item->setSessionCrop(state.hasCrop, state.cropRect);
        item->setColorAdjustmentsRecord(state.colorAdjust);
        item->setAppliedContentXform(ContentXform::Value::fromState(state));
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
    if (state.hasCrop && !state.cropRect.isEmpty()) {
        const QSize baked = item->sourceImage().size();
        if (baked.width() > 1 && baked.height() > 1) {
            item->setIntrinsicSize(baked);
        }
    } else {
        const QSize cur = item->imageSize();
        if (isPositiveSize(cur) && cur.width() > 1 && cur.height() > 1) {
            const QSize oriented = ContentXform::layoutSize(cur, state);
            if (oriented != cur) {
                item->setIntrinsicSize(oriented);
            }
        }
    }
    item->setAppliedContentXform(ContentXform::Value::fromState(state));
}

void syncItemLayoutToContentOrientation(ImageItem *item,
                                        const WorkspaceItemState &state)
{
    // Layout is owned by attachDisplaySample / ContentXform::layoutSize(native, want).
    // No aspect heuristics — callers must pass file-native size into layoutSize.
    Q_UNUSED(item);
    Q_UNUSED(state);
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
