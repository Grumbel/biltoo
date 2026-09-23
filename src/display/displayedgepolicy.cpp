// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displayedgepolicy.h"

#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "view/viewtransform.h"

#include <QtGlobal>
#include <QtMath>

namespace DisplayEdgePolicy {

int cappedDisplayEdge(int wantEdge, int nativeLongEdge)
{
    // Ladder steps are discrete (…1024, 2048). Never request past the known
    // native long edge — PreferCache cannot invent pixels, and the HUD must
    // not claim "→2048" for a 1920×1080 photo.
    int edge = wantEdge > 0 ? wantEdge : ThumtooCache::kImageLadderEdge;
    edge = qMin(edge, ThumtooCache::kImageLadderEdge);
    if (nativeLongEdge > 0) {
        edge = qMin(edge, nativeLongEdge);
    }
    // Snap up only within the remaining budget (ceil then clamp to native again).
    edge = ThumtooCache::ceilLadderEdge(edge);
    if (nativeLongEdge > 0) {
        edge = qMin(edge, nativeLongEdge);
    }
    return ViewTransform::atLeast1(edge);
}

bool sampleCoversNative(int sampleLongEdge, int nativeLongEdge, bool nativeKnown,
                        int overviewEdge, int imageLadderEdge)
{
    if (sampleLongEdge <= 0) {
        return false;
    }
    if (sampleLongEdge <= overviewEdge) {
        return false;
    }
    if (!nativeKnown || nativeLongEdge <= 0) {
        return sampleLongEdge >= imageLadderEdge;
    }
    return coversEdge(sampleLongEdge, nativeLongEdge);
}


QImage clampSoftForCell(const QImage &pixels, int needEdge, int minEdge)
{
    const int have = ImageCache::longEdge(pixels);
    if (needEdge <= 0 || have <= needEdge * 2) {
        return pixels;
    }
    const int target = qMax(needEdge, minEdge);
    if (have <= target) {
        return pixels;
    }
    return ImageCache::clampToMaxEdge(pixels, target);
}

int needEdgeFromScreenLongPx(qreal longPx, bool allowHighRes)
{
    const int need = ThumtooCache::ceilLadderEdge(int(qCeil(longPx)));
    if (!allowHighRes) {
        return qMin(need, ThumtooCache::kGalleryLadderEdge);
    }
    return need;
}


QualityTier classifyQualityTier(int displayLongEdge, int nativeLongEdge,
                                 bool hasDecodedPixels, int overviewEdge,
                                 int galleryEdge, int filmstripEdge,
                                 int lqipMaxEdge)
{
    if (displayLongEdge <= 0) {
        return QualityTier::Loading;
    }
    if (hasDecodedPixels && nativeLongEdge > 0
        && coversEdge(displayLongEdge, nativeLongEdge)) {
        return QualityTier::FullResolution;
    }
    if (displayLongEdge >= overviewEdge) {
        return QualityTier::HighQuality;
    }
    if (displayLongEdge >= galleryEdge) {
        return QualityTier::Preview;
    }
    if (displayLongEdge >= filmstripEdge) {
        return QualityTier::Thumbnail;
    }
    if (displayLongEdge <= lqipMaxEdge) {
        return QualityTier::Placeholder;
    }
    return QualityTier::QuickPreview;
}

} // namespace DisplayEdgePolicy
