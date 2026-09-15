// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYQUALITY_H
#define DISPLAYQUALITY_H

#include <QString>

/**
 * Host-side display quality helpers (edge / tier only).
 *
 * **Install policy** is DisplaySurface::decide (docs/DISPLAY_SURFACE.md).
 * Do not reintroduce checkSurface host-vs-shown install drivers.
 */
namespace DisplayQuality {

/** Long-edge ceiling for LQIP / "quick preview" (ThumbHash-scale). */
constexpr int kLqipMaxEdge = 96;

/** Soft ladder durable max (matches ThumtooCache::kGalleryLadderEdge). */
constexpr int kSoftMaxEdge = 512;

enum class Tier {
    Blank = 0,
    Lqip = 1,       ///< ≤ kLqipMaxEdge
    Soft = 2,       ///< ≤ kSoftMaxEdge
    Overview = 3,   ///< ≤ 1024
    Full = 4
};

Tier tierOf(int longEdge);

/** True when incoming is a meaningful upgrade over what is shown (edge-only). */
bool isStrictUpgrade(int shownLongEdge, int incomingLongEdge);

/** ImageCache long edge for path, or 0. */
int hostLongEdge(const QString &path);

} // namespace DisplayQuality

#endif
