// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayedgepolicy.h"

#include "thumtoocache.h"

#include <QtGlobal>

namespace DisplayEdgePolicy {

namespace {
constexpr int kCoverNumer = 9;
constexpr int kCoverDenom = 10;
} // namespace

bool coversEdge(int haveLongEdge, int targetEdge)
{
    return targetEdge <= 0
        || haveLongEdge >= (targetEdge * kCoverNumer) / kCoverDenom;
}

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
    return qMax(1, edge);
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

} // namespace DisplayEdgePolicy
