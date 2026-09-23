// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshow/slideshowatlaspolicy.h"

#include "host/thumtoocache.h"
#include "view/viewtransform.h"

#include <QtGlobal>
#include <QtMath>

namespace SlideshowAtlasPolicy {

namespace {
constexpr int kAdequacyNumer = 7;
constexpr int kAdequacyDenom = 10;
} // namespace

bool coversSource(const QPixmap &atlas, qreal atlasScale, int atlasVw, int atlasVh,
                  const DwellAtlasParams &params, const QImage &source)
{
    if (!params.valid || atlas.isNull() || source.isNull()) {
        return false;
    }
    if (!qFuzzyCompare(atlasScale, params.keyScale) || atlasVw != params.vw
        || atlasVh != params.vh || atlas.width() < params.longCap) {
        return false;
    }
    const int have = qMax(atlas.width(), atlas.height());
    const int srcLong = qMax(source.width(), source.height());
    // Atlas is always ~longCap (viewport budget). Soft samples are *upscaled*
    // into it, so srcLong << have does NOT mean the atlas is sharp — the old
    // "srcLong <= have*5/4 → cover" test left soft-looking atlases on screen
    // forever while PreferCache delivered 1024/native.
    //
    // Soft band only: keep the soft-upscaled atlas (skip 256↔512 thrash).
    // Above soft: require a rebuild so HQ replaces the soft upsample.
    // At/above longCap: atlas is adequate if it fills the budget.
    if (srcLong >= params.longCap) {
        return have >= params.longCap;
    }
    if (srcLong <= ThumtooCache::kGalleryLadderEdge) {
        return true;
    }
    // PreferCache mid/high sample while atlas is still a soft upsample.
    return false;
}

qreal motionHeadroom(SlideshowMotion motion, qreal panZoomFactor, bool progressActive)
{
    // Ken Burns / pan-scan sample past 1:1 cover; need extra source pixels or
    // the zoomed region is soft. Off → 1.0. PanZoom uses the configured factor.
    if (!progressActive || motion == SlideshowMotion::Off) {
        return 1.0;
    }
    if (motion == SlideshowMotion::PanZoom) {
        return clampPanZoomHeadroom(panZoomFactor);
    }
    // PanScan can raise scale when travel is short.
    return 1.25;
}

int needEdge(int targetEdge)
{
    return targetEdge * kAdequacyNumer / kAdequacyDenom;
}

int targetLongEdge(bool viewportValid, int viewportW, int viewportH, qreal dpr,
                   qreal headroom)
{
    // Screen-fit only: viewport × DPR × Ken-Burns headroom. Never native /
    // whole-image PreferCache Full — tiles (TileSynth SoftDisplay) or soft at
    // this edge. Cap at overview band so 4K+ natives are not pulled in whole.
    if (!viewportValid) {
        return ThumtooCache::kGalleryLadderEdge;
    }
    const int longPx =
        int(qCeil(qMax(viewportW, viewportH) * dpr * headroom));
    const int snapped = ThumtooCache::ceilLadderEdge(qMax(longPx, 256));
    return qMin(snapped, ThumtooCache::kBatchOverviewEdge);
}

DwellAtlasParams makeParams(int viewportW, int viewportH, qreal headroom)
{
    DwellAtlasParams p;
    p.vw = ViewTransform::atLeast1(viewportW);
    p.vh = ViewTransform::atLeast1(viewportH);
    p.headroom = headroom;
    p.longCap = int(qCeil(qreal(qMax(p.vw, p.vh)) * p.headroom));
    p.keyScale = p.headroom;
    p.valid = p.longCap > 0;
    return p;
}


qreal zoomBaseScale(SlideshowZoom zoom, const QSize &logical, int vw, int vh)
{
    if (!logical.isValid() || logical.width() < 1 || logical.height() < 1) {
        return 1.0;
    }
    const qreal iw = qreal(logical.width());
    const qreal ih = qreal(logical.height());
    const qreal w = qreal(ViewTransform::atLeast1(vw));
    const qreal h = qreal(ViewTransform::atLeast1(vh));
    switch (zoom) {
    case SlideshowZoom::Fill:
        return ViewTransform::coverScale(w, h, iw, ih);
    case SlideshowZoom::Actual:
        return 1.0;
    case SlideshowZoom::Fit:
    default:
        return ViewTransform::containScale(w, h, iw, ih);
    }
}

} // namespace SlideshowAtlasPolicy
