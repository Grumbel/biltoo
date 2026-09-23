// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "slideshowphasepolicy.h"

#include "display/imagecache.h"

namespace SlideshowPhasePolicy {

int longEdge(const QImage &img)
{
    return ImageCache::longEdge(img);
}

bool bufferWantsSample(const QString &phasePath, const QImage &phaseImg,
                       bool contentApplied, const QString &incomingPath,
                       int sampleEdge, bool hasPendingContentAppearance)
{
    if (sampleEdge <= 0 || incomingPath.isEmpty() || incomingPath != phasePath) {
        return false;
    }
    const int have = longEdge(phaseImg);
    if (sampleEdge > have) {
        return true;
    }
    if (sampleEdge < have) {
        return false;
    }
    // Same edge: still want when ContentXform is pending (arm used unoriented
    // stand-ins; orient may not grow long-edge for flip-only).
    if (contentApplied) {
        return false;
    }
    return hasPendingContentAppearance;
}

bool acceptOrientedUpgrade(int sampleEdge, int haveEdge, bool contentApplied)
{
    // Sharper always; same edge when ContentXform not yet applied on the arm.
    return sampleEdge > haveEdge || (sampleEdge == haveEdge && !contentApplied);
}

} // namespace SlideshowPhasePolicy
