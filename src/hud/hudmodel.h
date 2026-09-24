// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDMODEL_H
#define HUDMODEL_H

#include "display/displayedgepolicy.h"

#include <QString>
#include <QSize>
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
 * @p edge painted long edge; @p native logical native long edge (0 unknown).
 * Gallery: when under need, "Tier · havePx (need Npx)"; else tier only.
 * Image: when below native, "Tier · edgePx of nativePx"; else tier only.
 * Outer assemblers must not re-append edge when this already contains "px".
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

/** Image-mode status header: "W×H · Zoom Z%". */
QString imageModeStatusHeader(int nativeWidth, int nativeHeight, int zoomPercent);

/** " · quality" or " · quality (Npx)" when @p appendEdgePx and edge > 0. Empty if quality empty. */
QString qualityStatusSuffix(const QString &quality, int edge, bool appendEdgePx);

/**
 * " · W×H" for real native sizes. Skips empty/provisional and placeholder
 * 1000×1000 / 1024×1024 probes.
 */
QString nativeSizeStatusSuffix(const QSize &native);

/** " · Loading N…" when pending > 0. */
QString pendingLoadStatusSuffix(int pending);

/** " · label" when @p label non-empty (ThumtooCache loading breakdown, climb activity). */
QString labeledStatusSuffix(const QString &label);

/** " · Edited" when content appearance is non-identity. */
QString editedStatusSuffix(bool edited);

/** Display name with optional " · modified" content-edit mark. */
QString fileNameWithModifiedSuffix(const QString &displayName, bool modified);

/**
 * Image-mode quality suffix: append "(Npx)" only when the quality label does
 * not already embed a px figure and pixels are still provisional.
 */
bool shouldAppendQualityEdgePx(int edge, bool hasDecodedPixels, const QString &quality);

/** True when THUMTOO_DEBUG is set to a non-empty, non-"0" value. */
bool isThumtooDebugEnabled();

/**
 * Optional " · via source" / " · queue" suffix for THUMTOO_DEBUG status lines.
 * Empty when both inputs are empty.
 */
QString thumtooDebugStatusSuffix(const QString &pixelSourceLabel,
                                 const QString &queueStatsLabel);


/**
 * Assemble multi-item (Gallery/Workspace) status bar line from precomputed bits.
 * Pure; no ImageView dependency.
 */
QString formatMultiItemStatusLine(
    bool galleryMode, int itemCount, int zoomPercent,
    const QString &quality, int edge, const QSize &native,
    bool thumtooDebugGalleryMix, int blank, int lqip, int soft, int higher, int climbing,
    int pendingDecodeCount, const QString &loadingBreakdown,
    bool workspaceSelected, qreal scaleX, qreal scaleY, qreal rotationDegrees,
    bool edited, const QString &thumtooDebugSuffix);

/**
 * Assemble Image-mode status bar line from precomputed bits.
 */
QString formatImageModeStatusLine(
    int nativeWidth, int nativeHeight, int zoomPercent,
    const QString &quality, int edge, bool appendQualityEdgePx,
    const QString &climbActivityLabel,
    qreal rotationDegrees, bool hFlip, bool vFlip,
    bool edited, const QString &thumtooDebugSuffix);

} // namespace HudModel

#endif // HUDMODEL_H
