// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displayquality.h"

#include "display/imagecache.h"

#include <atomic>

namespace DisplayQuality {

namespace {
std::atomic<bool> g_smoothScaling{true};
} // namespace

void setSmoothScaling(bool on)
{
    g_smoothScaling.store(on, std::memory_order_relaxed);
}

bool smoothScaling()
{
    return g_smoothScaling.load(std::memory_order_relaxed);
}

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
    if (longEdge <= kOverviewMaxEdge) {
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
