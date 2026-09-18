// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowatlaspolicy.h"

#include "thumtoocache.h"

#include <QtGlobal>

namespace SlideshowAtlasPolicy {

bool coversSource(const QPixmap &atlas, qreal atlasScale, int atlasVw, int atlasVh,
                  const DwellAtlasParams &params, const QImage &source)
{
    if (!params.valid || atlas.isNull() || source.isNull()) {
        return false;
    }
    if (!qFuzzyCompare(atlasScale, params.keyScale) || atlasVw != params.vw
        || atlasVh != params.vh || atlas.width() < params.longCap * 9 / 10) {
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
    if (srcLong >= (params.longCap * 9) / 10) {
        return have >= (params.longCap * 9) / 10;
    }
    if (srcLong <= ThumtooCache::kGalleryLadderEdge) {
        return true;
    }
    // PreferCache mid/high sample while atlas is still a soft upsample.
    return false;
}

} // namespace SlideshowAtlasPolicy
