// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

#include <QtMath>

namespace ContentXform {

int normalizeQuarterTurns(int quarterTurns)
{
    quarterTurns %= 4;
    if (quarterTurns < 0) {
        quarterTurns += 4;
    }
    return quarterTurns;
}

Value Value::fromState(const WorkspaceItemState &s)
{
    Value x;
    x.quarterTurns = normalizeQuarterTurns(s.contentQuarterTurns);
    x.hFlip = s.contentHFlip;
    x.vFlip = s.contentVFlip;
    x.hasCrop = s.hasCrop && !s.cropRect.isEmpty();
    x.cropRect = s.cropRect;
    x.cropSourceSize = s.cropSourceSize;
    x.cropRotation = s.cropRotation;
    x.colorAdjust = s.colorAdjust;
    return x;
}

void Value::applyToState(WorkspaceItemState &s) const
{
    s.contentQuarterTurns = normalizeQuarterTurns(quarterTurns);
    s.contentHFlip = hFlip;
    s.contentVFlip = vFlip;
    s.hasCrop = hasCrop;
    s.cropRect = cropRect;
    s.cropSourceSize = cropSourceSize;
    s.cropRotation = cropRotation;
    s.colorAdjust = colorAdjust;
}

bool swapsAspect(const Value &x)
{
    return (normalizeQuarterTurns(x.quarterTurns) % 2) != 0;
}

bool equal(const Value &a, const Value &b)
{
    if (normalizeQuarterTurns(a.quarterTurns) != normalizeQuarterTurns(b.quarterTurns)) {
        return false;
    }
    if (a.hFlip != b.hFlip || a.vFlip != b.vFlip) {
        return false;
    }
    if (a.hasCrop != b.hasCrop) {
        return false;
    }
    if (a.hasCrop) {
        if (a.cropRect != b.cropRect || a.cropSourceSize != b.cropSourceSize) {
            return false;
        }
        if (!qFuzzyCompare(1.0 + a.cropRotation, 1.0 + b.cropRotation)) {
            return false;
        }
    }
    const ColorAdjustments &ca = a.colorAdjust;
    const ColorAdjustments &cb = b.colorAdjust;
    if (ca.brightness != cb.brightness || ca.contrast != cb.contrast
        || ca.saturation != cb.saturation || ca.hue != cb.hue
        || ca.invert != cb.invert
        || !qFuzzyCompare(ca.gamma, cb.gamma)) {
        return false;
    }
    return true;
}

QRect mapCropRectThroughContentRotate90(QRect crop, QSize &space, int quarterTurns)
{
    quarterTurns = normalizeQuarterTurns(quarterTurns);
    if (quarterTurns == 0 || crop.isEmpty()) {
        return crop.normalized();
    }
    if (space.width() < 1 || space.height() < 1) {
        // Degenerate canvas: still swap axes of the rect for odd turns.
        QRect r = crop.normalized();
        if ((quarterTurns % 2) != 0) {
            r = QRect(r.y(), r.x(), r.height(), r.width());
            space = QSize(space.height(), space.width());
        }
        return r;
    }
    QRect r = crop.normalized();
    // Match QImage::trueMatrix(QTransform::rotate(90*k), W, H):
    // one +90° step: (x,y,w,h) on (W,H) → (H-y-h, x, h, w) on (H,W).
    for (int i = 0; i < quarterTurns; ++i) {
        const int W = space.width();
        const int H = space.height();
        r = QRect(H - r.y() - r.height(), r.x(), r.height(), r.width());
        if (r.width() < 1) {
            r.setWidth(1);
        }
        if (r.height() < 1) {
            r.setHeight(1);
        }
        space = QSize(H, W);
    }
    return r.normalized();
}

void mapCropThroughContentRotate90(Value &x, int quarterTurns)
{
    if (!x.hasCrop || x.cropRect.isEmpty() || quarterTurns == 0) {
        return;
    }
    quarterTurns = normalizeQuarterTurns(quarterTurns);
    if (quarterTurns == 0) {
        return;
    }
    QSize sz = x.cropSourceSize;
    if (!sz.isValid() || sz.width() < 1 || sz.height() < 1) {
        const QRect r = x.cropRect.normalized();
        sz = QSize(r.x() + r.width(), r.y() + r.height());
    }
    x.cropRect = mapCropRectThroughContentRotate90(x.cropRect, sz, quarterTurns);
    x.cropSourceSize = sz;
    x.cropRotation -= 90.0 * quarterTurns;
    // Normalize cropRotation into (-180, 180].
    while (x.cropRotation > 180.0) {
        x.cropRotation -= 360.0;
    }
    while (x.cropRotation <= -180.0) {
        x.cropRotation += 360.0;
    }
}

/** True when both sizes share landscape/portrait class (or either is square). */
static bool sameOrientationClass(const QSize &a, const QSize &b)
{
    if (a.width() < 1 || a.height() < 1 || b.width() < 1 || b.height() < 1) {
        return true;
    }
    if (a.width() == a.height() || b.width() == b.height()) {
        return true;
    }
    return (a.width() > a.height()) == (b.width() > b.height());
}

static QRect scaleCropRectLocal(const QRect &crop, const QSize &recorded, const QSize &live)
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

QSize layoutSize(const QSize &native, const Value &x)
{
    if (!isPositiveSize(native)) {
        return native;
    }
    // Post-orient size first (cropRect is defined in that space).
    QSize oriented = swapsAspect(x) ? QSize(native.height(), native.width())
                                    : native;
    if (!x.hasCrop || x.cropRect.isEmpty()) {
        return oriented;
    }
    QSize basis = (x.cropSourceSize.isValid() && x.cropSourceSize.width() > 0
                   && x.cropSourceSize.height() > 0)
                      ? x.cropSourceSize
                      : oriented;
    QRect crop = x.cropRect.normalized();

    // Orientation mismatch: crop was recorded in a space whose axes do not
    // match the current oriented frame (e.g. turns bumped without mapping the
    // crop). Linear scale alone is wrong unless crop aspect == full aspect.
    // Map through one 90° step (same matrix as materialize / bakeRotate90).
    if (!sameOrientationClass(basis, oriented)) {
        crop = mapCropRectThroughContentRotate90(crop, basis, 1);
        // If still mismatched (shouldn't happen for axis-aligned sizes), try
        // one more step so basis matches oriented class.
        if (!sameOrientationClass(basis, oriented)) {
            crop = mapCropRectThroughContentRotate90(crop, basis, 1);
        }
    }

    if (basis != oriented) {
        crop = scaleCropRectLocal(crop, basis, oriented);
    }
    if (crop.width() < 1 || crop.height() < 1) {
        return oriented;
    }
    return QSize(crop.width(), crop.height());
}

QSize layoutSize(const QSize &native, const WorkspaceItemState &state)
{
    return layoutSize(native, Value::fromState(state));
}

bool needsRematerialize(const Value &applied, const Value &want,
                        int shownLongEdge, int incomingLongEdge)
{
    if (incomingLongEdge <= 0) {
        return false;
    }
    if (shownLongEdge <= 0) {
        return true;
    }
    if (!equal(applied, want)) {
        return true;
    }
    return incomingLongEdge > shownLongEdge;
}

QSizeF scaleToPreserveFootprint(qreal footW, qreal footH, const QSize &logical)
{
    if (!isPositiveSize(logical) || logical.width() < 1 || logical.height() < 1) {
        return QSizeF(1.0, 1.0);
    }
    if (!(footW > 1e-6) || !(footH > 1e-6)) {
        return QSizeF(1.0, 1.0);
    }
    const qreal sx = footW / qreal(logical.width());
    const qreal sy = footH / qreal(logical.height());
    if (!qIsFinite(sx) || !qIsFinite(sy) || sx < 1e-6 || sy < 1e-6) {
        return QSizeF(1.0, 1.0);
    }
    return QSizeF(sx, sy);
}

} // namespace ContentXform
