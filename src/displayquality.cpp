// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displayquality.h"

#include "imagecache.h"

namespace DisplayQuality {

Tier tierOf(int longEdge)
{
    if (longEdge <= 0) {
        return Tier::Blank;
    }
    if (longEdge <= kLqipMaxEdge) {
        return Tier::Lqip;
    }
    if (longEdge <= kSoftMaxEdge) {
        return Tier::Soft;
    }
    if (longEdge <= 1024) {
        return Tier::Overview;
    }
    return Tier::Full;
}

bool isStrictUpgrade(int shownLongEdge, int incomingLongEdge)
{
    if (incomingLongEdge <= 0) {
        return false;
    }
    if (shownLongEdge <= 0) {
        return true;
    }
    return incomingLongEdge > shownLongEdge;
}

int hostLongEdge(const QString &path)
{
    if (path.isEmpty()) {
        return 0;
    }
    return ImageCache::longEdge(ImageCache::get(path));
}

} // namespace DisplayQuality
