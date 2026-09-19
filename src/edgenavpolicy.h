// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef EDGENAVPOLICY_H
#define EDGENAVPOLICY_H

#include <QPoint>
#include <QRect>
#include <QtGlobal>

/**
 * Pure Image-mode edge chrome geometry (prev / next / gallery return).
 * Callers still gate mode (Image vs crop/attention) before consulting this.
 */
namespace EdgeNavPolicy {

constexpr int kZoneWidthFloor = 48;
constexpr qreal kZoneWidthFrac = 0.12;
constexpr int kZoneHeightFloor = 40;
constexpr qreal kZoneHeightFrac = 0.10;
constexpr int kDefaultButtonRadius = 22;
constexpr int kDefaultMargin = 10;

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

/** Gradient fill + chevron button centre for a hovered edge zone. */
struct ChromeLayout {
    QRect fillRect;
    QPoint buttonCenter;
};

/**
 * Layout for paint: gradient strip and button centre in viewport coords.
 * @p zoneW / @p zoneH from zoneWidth / zoneHeight.
 * Button radius is caller-owned (paint uses 22); margin is 10.
 */
ChromeLayout chromeLayout(Zone zone, const QRect &viewport, int zoneW, int zoneH,
                          int buttonRadius = kDefaultButtonRadius, int margin = kDefaultMargin);

} // namespace EdgeNavPolicy

#endif // EDGENAVPOLICY_H
