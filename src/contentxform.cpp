// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "contentxform.h"

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

QSize layoutSize(const QSize &native, const Value &x)
{
    if (!isPositiveSize(native)) {
        return native;
    }
    if (swapsAspect(x)) {
        return QSize(native.height(), native.width());
    }
    return native;
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

} // namespace ContentXform
