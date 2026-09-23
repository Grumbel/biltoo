// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "hudmodel.h"

#include "session/sessionchrome.h"
#include "displayedgepolicy.h"

#include <QCoreApplication>

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

} // namespace HudModel
