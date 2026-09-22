// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lqipdisplaypolicy.h"

#include "biltoo_thread.h"
#include "displayquality.h"
#include "imagecache.h"
#include "thumtoocache.h"

namespace LqipDisplayPolicy {

QImage lqipOrCachedSample(const QString &path)
{
    ASSERT_NOT_GUI_THREAD();
    if (path.isEmpty()) {
        return {};
    }
    // LQIP / existing host sample only. Gallery is tiles-only: never PreferCache@soft
    // or classic loadThumbnail for underlay.
    QImage preview = ImageCache::get(path);
    if (!preview.isNull()
        && ImageCache::longEdge(preview) <= DisplayQuality::kLqipMaxEdge) {
        return preview;
    }
    preview = ThumtooCache::cachedLqipImage(path);
    if (!preview.isNull()) {
        ImageCache::put(path, preview);
        return preview;
    }
    // Keep a smaller host sample if present; do not encode soft.
    preview = ImageCache::get(path);
    if (!preview.isNull()
        && ImageCache::longEdge(preview) <= DisplayQuality::kLqipMaxEdge) {
        return preview;
    }
    return {};
}


bool galleryWithinLqipBand(int incomingEdge, int lqipMaxEdge)
{
    return incomingEdge > 0 && incomingEdge <= lqipMaxEdge;
}

bool galleryAcceptsLqipUpgrade(int shownEdge, int incomingEdge, int lqipMaxEdge)
{
    return shownEdge <= lqipMaxEdge
        && incomingEdge > shownEdge
        && incomingEdge <= lqipMaxEdge;
}


PathHaveEdge aggregatePathHaveEdge(const int *displayEdges, const bool *decoded,
                                   int count)
{
    PathHaveEdge out;
    if (!displayEdges || count <= 0) {
        return out;
    }
    for (int i = 0; i < count; ++i) {
        if (decoded && decoded[i]) {
            out.anyFull = true;
        }
        if (displayEdges[i] > out.have) {
            out.have = displayEdges[i];
        }
    }
    return out;
}

} // namespace LqipDisplayPolicy
