// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "itemframegeometry.h"
#include "viewtransform.h"

#include <QLineF>
#include <QtMath>

namespace ItemFrameGeometry {

QPointF unitOr(const QPointF &v, const QPointF &fallback)
{
    const qreal len = qHypot(v.x(), v.y());
    return len > 1e-6 ? v / len : fallback;
}

FrameViewGeom makeFrameViewGeom(const QPointF &tl, const QPointF &tr,
                                const QPointF &br, const QPointF &bl)
{
    FrameViewGeom g;
    g.tl = tl;
    g.tr = tr;
    g.br = br;
    g.bl = bl;
    g.center = (tl + tr + br + bl) * 0.25;
    g.dirTop = unitOr(tr - tl);
    g.dirRight = unitOr(br - tr);
    g.dirBottom = unitOr(bl - br);
    g.dirLeft = unitOr(tl - bl);
    g.midTop = (tl + tr) * 0.5;
    g.midRight = (tr + br) * 0.5;
    g.midBottom = (br + bl) * 0.5;
    g.midLeft = (bl + tl) * 0.5;
    auto outward = [&](const QPointF &mid, const QPointF &along) {
        QPointF n(-along.y(), along.x());
        if (QPointF::dotProduct(n, mid - g.center) < 0) {
            n = -n;
        }
        return n;
    };
    g.outTop = outward(g.midTop, g.dirTop);
    g.outRight = outward(g.midRight, g.dirRight);
    g.outBottom = outward(g.midBottom, g.dirBottom);
    g.outLeft = outward(g.midLeft, g.dirLeft);
    return g;
}

void chromeCentersView(const FrameViewGeom &g, QPointF outCenters[kChromeCount])
{
    const qreal btn = kChromeBtnScreenPx;
    const qreal step = btn + kChromeBtnGapPx;
    const qreal colOffset = kChromeOutsidePx + btn * 0.5;
    const QPointF colBase = g.tr + g.outRight * colOffset;
    const QPointF along = g.dirRight; // top → bottom along the right edge

    const QPointF rotR = g.midRight + g.outRight * kRotateOffsetPx;
    const qreal rotClear = kHandleScreenPx * 0.5 + kChromeClearPx + btn * 0.5;
    auto distAlong = [&](const QPointF &p) {
        return QPointF::dotProduct(p - colBase, along);
    };
    const qreal rotAlong = distAlong(rotR);
    const qreal lateral = qAbs(colOffset - kRotateOffsetPx);
    const qreal needAlongClear = qMax(0.0, rotClear - lateral);

    // Reserved band around the free-rotate knob.
    const qreal upperLastAlong = rotAlong - needAlongClear - kChromeGroupGapPx * 0.5;
    const qreal lowerFirstAlong = rotAlong + needAlongClear + kChromeGroupGapPx * 0.5;

    // Upper group: prefer flush with the top-right corner when it fits above
    // the reserved band; otherwise pack against the band from above.
    const qreal preferTop = btn * 0.5 + 4.0;
    qreal firstUpper = preferTop;
    if (preferTop + (kChromeUpperCount - 1) * step > upperLastAlong) {
        firstUpper = upperLastAlong - (kChromeUpperCount - 1) * step;
    }

    // Lower group: prefer flush with the bottom-right corner when it fits below
    // the reserved band; otherwise pack against the band from below.
    const qreal edgeLen = distAlong(g.br + g.outRight * colOffset);
    const qreal preferBottomFirst =
        edgeLen - ((kChromeLowerCount - 1) * step + btn * 0.5 + 4.0);
    qreal firstLower = preferBottomFirst;
    if (preferBottomFirst < lowerFirstAlong) {
        firstLower = lowerFirstAlong;
    }

    for (int i = 0; i < kChromeUpperCount; ++i) {
        outCenters[i] = colBase + along * (firstUpper + i * step);
    }
    for (int i = 0; i < kChromeLowerCount; ++i) {
        outCenters[kChromeUpperCount + i] = colBase + along * (firstLower + i * step);
    }
}

void opacityTrackView(const FrameViewGeom &g, QPointF *aOut, QPointF *bOut)
{
    // a = bottom end (opacity 5%), b = top end (opacity 100%).
    // Track length is always kSliderWidthPx (never shrinks).
    const QPointF alongUp = g.dirLeft; // bl → tl
    const qreal outDist = kSliderOutsidePx + kSliderHeightPx * 0.5;
    const qreal trackLen = kSliderWidthPx;
    const qreal cornerMargin = kHandleScreenPx * 0.6;

    auto projFromBl = [&](const QPointF &p) {
        return QPointF::dotProduct(p - g.bl, alongUp);
    };

    const QPointF rotL = g.midLeft + g.outLeft * kRotateOffsetPx;
    const qreal rotAlong = projFromBl(rotL);
    const qreal needClear = kHandleScreenPx * 0.5 + kSliderClearPx;
    const qreal maxTop = rotAlong - needClear;

    // Prefer bottom-anchored (clear of corner scale handle).
    qreal aAlong = cornerMargin;
    qreal bAlong = aAlong + trackLen;
    if (bAlong > maxTop) {
        // Not enough free space under the rotate knob: pin top to maxTop,
        // keep full track length (extends below the frame if needed).
        bAlong = maxTop;
        aAlong = bAlong - trackLen;
    }

    const QPointF origin = g.bl + g.outLeft * outDist;
    if (aOut) {
        *aOut = origin + alongUp * aAlong;
    }
    if (bOut) {
        *bOut = origin + alongUp * bAlong;
    }
}

qreal trackParam(const QPointF &a, const QPointF &b, const QPointF &p)
{
    const QPointF ab = b - a;
    const qreal ab2 = QPointF::dotProduct(ab, ab);
    if (ab2 <= 1e-6) {
        return 0.0;
    }
    return ViewTransform::clamp01(QPointF::dotProduct(p - a, ab) / ab2);
}

qreal opacityFromTrackParam(qreal t)
{
    return kOpacityTrackMin + ViewTransform::clamp01(t) * kOpacityTrackSpan;
}

qreal trackParamFromOpacity(qreal opacity)
{
    if (kOpacityTrackSpan <= 1e-9) {
        return 0.0;
    }
    return ViewTransform::clamp01((opacity - kOpacityTrackMin) / kOpacityTrackSpan);
}

QPointF closestPointOnSegment(const QPointF &a, const QPointF &b, const QPointF &p)
{
    const qreal t = trackParam(a, b, p);
    return a + (b - a) * t;
}

qreal distanceToSegment(const QPointF &a, const QPointF &b, const QPointF &p)
{
    return QLineF(p, closestPointOnSegment(a, b, p)).length();
}

void rotateHandlePoints(const FrameViewGeom &g, QPointF out[4], qreal offsetPx)
{
    out[0] = g.midTop + g.outTop * offsetPx;
    out[1] = g.midRight + g.outRight * offsetPx;
    out[2] = g.midBottom + g.outBottom * offsetPx;
    out[3] = g.midLeft + g.outLeft * offsetPx;
}

void shearHandlePoints(const FrameViewGeom &g, QPointF out[4], qreal alongPx)
{
    out[0] = g.midTop - g.dirTop * alongPx;
    out[1] = g.midBottom + g.dirBottom * alongPx;
    out[2] = g.midLeft - g.dirLeft * alongPx;
    out[3] = g.midRight + g.dirRight * alongPx;
}

} // namespace ItemFrameGeometry
