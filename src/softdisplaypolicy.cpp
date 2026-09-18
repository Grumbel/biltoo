// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "softdisplaypolicy.h"

#include "biltoo_thread.h"
#include "displayquality.h"
#include "imagecache.h"
#include "thumtoocache.h"

namespace SoftDisplayPolicy {

QImage lqipOrCachedSoft(const QString &path)
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

} // namespace SoftDisplayPolicy
