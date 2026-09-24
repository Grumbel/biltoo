// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hud/hudmodel.h"

#include "session/sessionchrome.h"
#include "display/displayedgepolicy.h"

#include <QCoreApplication>
#include <QStringList>
#include <QSize>
#include <QtMath>

#include <cstdlib>

namespace HudModel {
namespace {

QString tr(const char *s)
{
    return QCoreApplication::translate("HudModel", s);
}

} // namespace

QString qualityTierLabel(DisplayEdgePolicy::QualityTier tier)
{
    using Tier = DisplayEdgePolicy::QualityTier;
    switch (tier) {
    case Tier::Loading:
        return tr("Loading…");
    case Tier::FullResolution:
        return tr("Full resolution");
    case Tier::HighQuality:
        return tr("High quality");
    case Tier::Preview:
        return tr("Preview");
    case Tier::Thumbnail:
        return tr("Thumbnail");
    case Tier::Placeholder:
        return tr("Placeholder");
    case Tier::QuickPreview:
    default:
        return tr("Quick preview");
    }
}

QString qualityLabelDetail(DisplayEdgePolicy::QualityTier tier, int edge, int native,
                           bool galleryMode, bool imageMode,
                           int galleryNeed, int galleryHave)
{
    using Tier = DisplayEdgePolicy::QualityTier;
    if (tier == Tier::Loading) {
        return qualityTierLabel(tier);
    }
    const QString tierLabel = qualityTierLabel(tier);
    if (tier == Tier::FullResolution) {
        return tierLabel;
    }
    if (galleryMode) {
        if (galleryNeed > 0 && galleryHave > 0) {
            return tr("%1 · show %2px · need %3px · have %4px")
                .arg(tierLabel)
                .arg(edge)
                .arg(galleryNeed)
                .arg(galleryHave);
        }
        return tierLabel;
    }
    if (imageMode && edge > 0) {
        if (native > 0 && !DisplayEdgePolicy::coversEdge(edge, native)) {
            return tr("%1 · show %2px · native %3px")
                .arg(tierLabel)
                .arg(edge)
                .arg(native);
        }
    }
    return tierLabel;
}

QString sessionBadge(int index, int total)
{
    const QString ascii = sessionBadgeAscii(index, total);
    if (ascii.isEmpty()) {
        return {};
    }
    return ascii;
}

QString emptyCanvasStatus(bool hasLoadError, const QString &loadErrorDisplayName,
                          bool hasClassicPath, bool imageMode,
                          bool galleryMode, bool workspaceMode)
{
    if (hasLoadError) {
        return tr("Failed to load “%1”").arg(loadErrorDisplayName);
    }
    if (hasClassicPath && imageMode) {
        return tr("Loading…");
    }
    if (galleryMode) {
        return tr("Gallery — no images");
    }
    if (workspaceMode) {
        return tr("Workspace — drop images or use Open");
    }
    return tr("Ready");
}

QString multiItemHeader(bool galleryMode, int itemCount, int zoomPercent)
{
    const QString modeLabel = galleryMode ? tr("Gallery") : tr("Workspace");
    return tr("%1 · %2 images · Zoom %3%")
        .arg(modeLabel)
        .arg(itemCount)
        .arg(zoomPercent);
}

QString loadingLineWithGalleryExtras(const QString &core, int blankTiles, int weakTiles)
{
    QStringList extra;
    if (blankTiles > 0) {
        extra << tr("%1 blank").arg(blankTiles);
    }
    if (weakTiles > 0) {
        extra << tr("%1 quick preview").arg(weakTiles);
    }
    if (core.isEmpty() && extra.isEmpty()) {
        return {};
    }
    if (core.isEmpty()) {
        return tr("Loading · %1").arg(extra.join(QStringLiteral(" · ")));
    }
    if (extra.isEmpty()) {
        return core;
    }
    return core + QStringLiteral(" · ") + extra.join(QStringLiteral(" · "));
}

QString placementFlipRotationSuffix(qreal rotationDegrees, bool hFlip, bool vFlip)
{
    QString text;
    if (qAbs(rotationDegrees) > 0.5) {
        text += tr(" · Rot %1°").arg(qRound(rotationDegrees));
    }
    if (hFlip || vFlip) {
        QStringList flips;
        if (hFlip) {
            flips << tr("H");
        }
        if (vFlip) {
            flips << tr("V");
        }
        text += tr(" · Flip %1").arg(flips.join(QLatin1Char('+')));
    }
    return text;
}


QString galleryDebugPixelMixSuffix(int blank, int lqip, int soft, int higher, int climbing)
{
    if (blank <= 0 && lqip <= 0 && soft <= 0 && higher <= 0 && climbing <= 0) {
        return {};
    }
    QString text = tr(" · %1 blank · %2 lqip · %3 soft · %4 higher")
                       .arg(blank)
                       .arg(lqip)
                       .arg(soft)
                       .arg(higher);
    if (climbing > 0) {
        text += tr(" · climbing %1").arg(climbing);
    }
    return text;
}

QString workspaceSelectedItemScaleSuffix(qreal scaleX, qreal scaleY, qreal rotationDegrees)
{
    const qreal sy = scaleY > 0.0 ? scaleY : scaleX;
    if (qAbs(scaleX - sy) < 0.005) {
        return tr(" · Item %1% · Rot %2°")
            .arg(qRound(scaleX * 100))
            .arg(qRound(rotationDegrees));
    }
    return tr(" · Item %1%×%2% · Rot %3°")
        .arg(qRound(scaleX * 100))
        .arg(qRound(sy * 100))
        .arg(qRound(rotationDegrees));
}


QString imageModeStatusHeader(int nativeWidth, int nativeHeight, int zoomPercent)
{
    return tr("%1×%2 · Zoom %3%")
        .arg(nativeWidth)
        .arg(nativeHeight)
        .arg(zoomPercent);
}


QString qualityStatusSuffix(const QString &quality, int edge, bool appendEdgePx)
{
    if (quality.isEmpty()) {
        return {};
    }
    if (appendEdgePx && edge > 0) {
        return tr(" · %1 (%2px)").arg(quality).arg(edge);
    }
    return tr(" · %1").arg(quality);
}

QString nativeSizeStatusSuffix(const QSize &native)
{
    if (!(native.width() > 1 && native.height() > 1)) {
        return {};
    }
    // Placeholder probe sizes from unknown-file defaults — not real native.
    if (native == QSize(1000, 1000) || native == QSize(1024, 1024)) {
        return {};
    }
    return tr(" · %1×%2").arg(native.width()).arg(native.height());
}

QString pendingLoadStatusSuffix(int pending)
{
    if (pending <= 0) {
        return {};
    }
    return tr(" · Loading %1…").arg(pending);
}

QString labeledStatusSuffix(const QString &label)
{
    if (label.isEmpty()) {
        return {};
    }
    return tr(" · %1").arg(label);
}

QString editedStatusSuffix(bool edited)
{
    if (!edited) {
        return {};
    }
    return tr(" · Edited");
}


QString fileNameWithModifiedSuffix(const QString &displayName, bool modified)
{
    if (displayName.isEmpty()) {
        return {};
    }
    if (modified) {
        return displayName + tr(" · modified");
    }
    return displayName;
}

bool shouldAppendQualityEdgePx(int edge, bool hasDecodedPixels, const QString &quality)
{
    // quality may already include "show Npx · native Mpx" — avoid double edge.
    return edge > 0 && !hasDecodedPixels && !quality.contains(QLatin1String("px"));
}

bool isThumtooDebugEnabled()
{
    const char *dbg = std::getenv("THUMTOO_DEBUG");
    return dbg && dbg[0] && dbg[0] != '0';
}

QString thumtooDebugStatusSuffix(const QString &pixelSourceLabel,
                                 const QString &queueStatsLabel)
{
    QString text;
    if (!pixelSourceLabel.isEmpty()) {
        text += tr(" · via %1").arg(pixelSourceLabel);
    }
    if (!queueStatsLabel.isEmpty()) {
        text += tr(" · %1").arg(queueStatsLabel);
    }
    return text;
}


QString formatMultiItemStatusLine(
    bool galleryMode, int itemCount, int zoomPercent,
    const QString &quality, int edge, const QSize &native,
    bool thumtooDebugGalleryMix, int blank, int lqip, int soft, int higher, int climbing,
    int pendingDecodeCount, const QString &loadingBreakdown,
    bool workspaceSelected, qreal scaleX, qreal scaleY, qreal rotationDegrees,
    bool edited, const QString &thumtooDebugSuffix)
{
    QString text = multiItemHeader(galleryMode, itemCount, zoomPercent);
    text += qualityStatusSuffix(quality, edge, edge > 0);
    text += nativeSizeStatusSuffix(native);
    if (thumtooDebugGalleryMix) {
        text += galleryDebugPixelMixSuffix(blank, lqip, soft, higher, climbing);
    }
    text += pendingLoadStatusSuffix(pendingDecodeCount);
    text += labeledStatusSuffix(loadingBreakdown);
    if (workspaceSelected) {
        text += workspaceSelectedItemScaleSuffix(scaleX, scaleY, rotationDegrees);
    }
    text += editedStatusSuffix(edited);
    text += thumtooDebugSuffix;
    return text;
}

QString formatImageModeStatusLine(
    int nativeWidth, int nativeHeight, int zoomPercent,
    const QString &quality, int edge, bool appendQualityEdgePx,
    const QString &climbActivityLabel,
    qreal rotationDegrees, bool hFlip, bool vFlip,
    bool edited, const QString &thumtooDebugSuffix)
{
    QString text = imageModeStatusHeader(nativeWidth, nativeHeight, zoomPercent);
    text += qualityStatusSuffix(quality, edge, appendQualityEdgePx);
    text += labeledStatusSuffix(climbActivityLabel);
    text += placementFlipRotationSuffix(rotationDegrees, hFlip, vFlip);
    text += editedStatusSuffix(edited);
    text += thumtooDebugSuffix;
    return text;
}

} // namespace HudModel
