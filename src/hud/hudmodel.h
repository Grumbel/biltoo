// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDMODEL_H
#define HUDMODEL_H

#include "display/displayedgepolicy.h"

#include <QString>
#include <QtGlobal>

/**
 * Pure HUD / status-bar formatting (Phase 6 Tier 3).
 *
 * Snapshot-driven strings with no QWidget / ImageView dependency so quality-tier
 * and session-badge labels are unit-testable without a QGraphicsView.
 */
namespace HudModel {

/** Human label for a quality tier (translated). */
QString qualityTierLabel(DisplayEdgePolicy::QualityTier tier);

/**
 * Full quality line for the status bar.
 * @p edge on-screen long edge; @p native logical native long edge (0 unknown).
 * Gallery path: when need/have > 0, appends "show · need · have" diagnostics.
 * Image path: when not full coverage, appends show vs native.
 */
QString qualityLabelDetail(DisplayEdgePolicy::QualityTier tier, int edge, int native,
                           bool galleryMode, bool imageMode,
                           int galleryNeed = 0, int galleryHave = 0);

/**
 * Session index badge "i/n" (1-based index, total count). Empty when invalid.
 * Pure wrapper over sessionBadgeAscii with optional translation hook.
 */
QString sessionBadge(int index, int total);

/** Empty-canvas status line from mode + load-error flags. */
QString emptyCanvasStatus(bool hasLoadError, const QString &loadErrorDisplayName,
                          bool hasClassicPath, bool imageMode,
                          bool galleryMode, bool workspaceMode);

/** Multi-item mode header: mode · N images · Zoom Z%. */
QString multiItemHeader(bool galleryMode, int itemCount, int zoomPercent);

/**
 * Combine ThumtooCache loading core with Gallery blank/weak tile counts.
 * Empty when both core and counts are empty.
 */
QString loadingLineWithGalleryExtras(const QString &core, int blankTiles, int weakTiles);

/**
 * Optional " · Rot N°" / " · Flip H+V" suffix from free-placement pose.
 * Empty when near-zero rotation and no flips.
 */
QString placementFlipRotationSuffix(qreal rotationDegrees, bool hFlip, bool vFlip);

/** THUMTOO_DEBUG gallery mix: blank / lqip / soft / higher [/ climbing]. */
QString galleryDebugPixelMixSuffix(int blank, int lqip, int soft, int higher, int climbing);

/** Workspace selected-item scale · rotation status suffix. */
QString workspaceSelectedItemScaleSuffix(qreal scaleX, qreal scaleY, qreal rotationDegrees);

} // namespace HudModel

#endif // HUDMODEL_H
