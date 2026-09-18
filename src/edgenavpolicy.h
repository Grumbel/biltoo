// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EDGENAVPOLICY_H
#define EDGENAVPOLICY_H

#include <QPoint>

/**
 * Pure Image-mode edge chrome geometry (prev / next / gallery return).
 * Callers still gate mode (Image vs crop/attention) before consulting this.
 */
namespace EdgeNavPolicy {

enum class Zone {
    None = 0,
    Previous,
    Next,
    GalleryReturn,
};

/** Left/right hit strip width from viewport width. */
int zoneWidth(int viewportWidth);

/** Top return strip height from viewport height. */
int zoneHeight(int viewportHeight);

/**
 * Hit-test @p viewPos in viewport CSS pixels.
 * @p galleryReturnAvailable enables the top strip; @p imageModeNavEnabled
 * enables left/right when return is not hit.
 */
Zone zoneAt(const QPoint &viewPos, int viewportWidth, int viewportHeight,
            bool galleryReturnAvailable, bool imageModeNavEnabled);

} // namespace EdgeNavPolicy

#endif // EDGENAVPOLICY_H
