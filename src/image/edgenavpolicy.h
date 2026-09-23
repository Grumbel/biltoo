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
 *
 * Left/right and top markers are half-ellipses covering about 80% of the edge
 * (centred), so top/bottom chrome (slideshow status, HUD) is not covered.
 */
namespace EdgeNavPolicy {

constexpr int kZoneWidthFloor = 48;
constexpr qreal kZoneWidthFrac = 0.12;
/** Top return strip — slightly shorter than left/right depth. */
constexpr int kZoneHeightFloor = 32;
constexpr qreal kZoneHeightFrac = 0.08;
/** Fraction of the long edge the marker spans (centred). */
constexpr qreal kEdgeSpanFrac = 0.80;
constexpr int kDefaultButtonRadius = 20;
constexpr int kDefaultMargin = 10;

enum class Zone {
    None = 0,
    Previous,
    Next,
    GalleryReturn,
};

/** Left/right hit strip depth from viewport width. */
int zoneWidth(int viewportWidth);

/** Top return strip depth from viewport height. */
int zoneHeight(int viewportHeight);

/** Centred span along the edge (80% of height for L/R, of width for top). */
int edgeSpanAlong(int edgeLength);

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
 * Layout for paint: half-ellipse bounding rect and button centre in viewport coords.
 * @p zoneW / @p zoneH from zoneWidth / zoneHeight.
 */
ChromeLayout chromeLayout(Zone zone, const QRect &viewport, int zoneW, int zoneH,
                          int buttonRadius = kDefaultButtonRadius, int margin = kDefaultMargin);

} // namespace EdgeNavPolicy

#endif // EDGENAVPOLICY_H
