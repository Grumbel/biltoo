// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sessionappearance.h"
#include "contentxform.h"
#include "biltoo_thread.h"
#include "coloradjust.h"

#include <QtMath>
#include <QImage>
#include <QPainter>
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
    // Pure geometry lives in ContentXform (same matrix as QImage::trueMatrix).
    ContentXform::Value x = ContentXform::Value::fromState(state);
    ContentXform::mapCropThroughContentRotate90(x, quarterTurns);
    state.hasCrop = x.hasCrop;
    state.cropRect = x.cropRect;
    state.cropSourceSize = x.cropSourceSize;
    state.cropRotation = x.cropRotation;
}

bool contentSwapsAspect(const WorkspaceItemState &state)
{
    return ContentXform::swapsAspect(ContentXform::Value::fromState(state));
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
    // Free rotation uses the same window sample as ImageItem::cropToLocalRect.
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
            const bool freeRot = qAbs(state.cropRotation) > 0.05;
            if (!freeRot) {
                const QRect bounds(0, 0, out.width(), out.height());
                const QRect srcRect = crop.intersected(bounds);
                if (srcRect.width() >= 1 && srcRect.height() >= 1) {
                    out = out.copy(srcRect);
                }
            } else {
                const int dw = qMax(1, crop.width());
                const int dh = qMax(1, crop.height());
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


void mergeAppliedAndLiveFlags(WorkspaceItemState &appearance,
                              const ContentXform::Value *appliedOrNull,
                              bool liveHFlip, bool liveVFlip,
                              bool liveHasCrop, const QRect &liveCropRect)
{
    // Applied fingerprint is authoritative when the store lagged a live edit
    // (rotate then fitItem before m_appearance was visible to this reader).
    if (appliedOrNull) {
        const ContentXform::Value &x = *appliedOrNull;
        if (appearance.contentQuarterTurns == 0 && x.quarterTurns != 0) {
            appearance.contentQuarterTurns = x.quarterTurns;
        }
        if (!appearance.contentHFlip && x.hFlip) {
            appearance.contentHFlip = true;
        }
        if (!appearance.contentVFlip && x.vFlip) {
            appearance.contentVFlip = true;
        }
    }
    // Live flags on the item win when the store is still empty for flips/crop.
    if (liveHFlip) {
        appearance.contentHFlip = true;
    }
    if (liveVFlip) {
        appearance.contentVFlip = true;
    }
    if (liveHasCrop && appearance.cropRect.isEmpty()) {
        appearance.hasCrop = true;
        appearance.cropRect = liveCropRect;
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
        m_seedAttempted.remove(id);
    }
}

void SessionAppearanceStore::clear()
{
    m_byId.clear();
    m_seedAttempted.clear();
}

bool SessionAppearanceStore::seedAttempted(SessionImageId id) const
{
    return id != kInvalidSessionImageId && m_seedAttempted.contains(id);
}

void SessionAppearanceStore::markSeedAttempted(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_seedAttempted.insert(id);
    }
}

void SessionAppearanceStore::clearSeedAttempted(SessionImageId id)
{
    if (id != kInvalidSessionImageId) {
        m_seedAttempted.remove(id);
    }
}
