// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "display/imagecache.h"
#include "host/thumtoocache.h"
#include "display/displayquality.h"
#include "display/pathrasterservice.h"
#include "host/archivepath.h"
#include "host/pagepath.h"
#include "hud/hudmodel.h"

#include <QFileInfo>
#include <QFontMetrics>
#include "content/contentxform.h"

void ImageView::refreshStatus()
{
    // Coalesce rapid soft-climb / provenance updates so the HUD and status
    // bar are not rewritten every frame.
    m_hud.scheduleStatusRefresh(this, [this]() {
        emit statusChanged();
        if ((m_hud.appearance().isVisible() || m_hud.flash().isVisible() || m_slideshow.hud().isPausedHud())
            && viewport()) {
            viewport()->update();
        }
    });
}

void ImageView::setHudVisible(bool on)
{
    if (!m_hud.appearance().setVisible(on)) {
        return;
    }
    m_slideshow.syncProgressTimerWithHud(on);
    viewport()->update();
}

void ImageView::setHudFontPointSize(int pt)
{
    if (!m_hud.appearance().setFontPointSize(pt)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudTextColor(const QColor &color)
{
    if (!m_hud.appearance().setTextColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudPanelColor(const QColor &color)
{
    if (!m_hud.appearance().setPanelColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hud.showFlash(action, detail, [this]() {
        if (viewport()) {
            viewport()->update();
        }
    });
}

QString ImageView::loadingStatusHudLine() const
{
    // Dedicated HUD line: job queue + cache vs file/archive + weak tiles.
    QString core = ThumtooCache::loadingBreakdownLabel();
    int blank = 0;
    int weak = 0;
    if (isGalleryMode()) {
        for (ImageItem *item : m_items) {
            if (!item || item->path().isEmpty()) {
                continue;
            }
            if (!item->hasDisplayPixels()) {
                ++blank;
            } else if (item->displayPixelLongEdge() > 0
                       && item->displayPixelLongEdge() < 96) {
                ++weak;
            }
        }
    }
    return HudModel::loadingLineWithGalleryExtras(core, blank, weak);
}

QString ImageView::hudFileName() const
{
    if (!m_gallery.hoverPath().isEmpty()) {
        return PagePath::displayName(m_gallery.hoverPath());
    }
    // Empty Gallery/Workspace canvas: only the drop invite is shown — never a
    // stale classicPath, load error, or leftover session subject filename.
    if (m_items.isEmpty() && isMultiItemMode()) {
        return {};
    }
    if (m_session.identity().hasLastLoadError()) {
        return PagePath::displayName(m_session.identity().lastLoadErrorRef());
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (item) {
        QString name = PagePath::displayName(item->path());
        if (targetHasContentAppearance()) {
            name += tr(" · modified");
        }
        return name;
    }
    if (m_image.hasClassicPath() && isImageMode()) {
        return PagePath::displayName(m_image.classicPath());
    }
    return {};
}

QString ImageView::pixelQualityLabel(const ImageItem *item) const
{
    if (!item) {
        return {};
    }
    // Prefer what is actually on screen over last thumtoo pipeline tag.
    // hasDecodedPixels alone is not "full": soft samples may have been installed
    // as FullSource by a mistaken PreferCache shortfall classification.
    const int edge = item->displayPixelLongEdge();
    const QSize logical = logicalSizeForPath(item->path());
    const int native = isPositiveSize(logical) ? ContentXform::longEdge(logical) : 0;
    using Tier = DisplayEdgePolicy::QualityTier;
    const Tier t = DisplayEdgePolicy::classifyQualityTier(
        edge, native, item->hasDecodedPixels(),
        ThumtooCache::kBatchOverviewEdge, ThumtooCache::kGalleryLadderEdge,
        ThumtooCache::kFilmstripLadderEdge, DisplayQuality::kLqipMaxEdge);
    int galleryNeed = 0;
    int galleryHave = 0;
    if (isGalleryMode()) {
        galleryNeed = m_displayPipeline->galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        const GalleryDecodeState *st = hostGalleryDecodeBook().get(item->path());
        galleryHave = st ? GalleryDecode::maxHave(st->have, edge) : edge;
    }
    return HudModel::qualityLabelDetail(
        t, edge, native, isGalleryMode(), isImageMode(), galleryNeed, galleryHave);
}

void ImageView::appendThumtooDebugStatus(QString *text, ImageItem *item) const
{
    if (!text || !item) {
        return;
    }
    // "via file decode" / ladder provenance is pipeline debug, not live status.
    // Showing it next to a stuck "Improving quality…" looked like an active decode.
    const char *dbg = std::getenv("THUMTOO_DEBUG");
    if (!dbg || !dbg[0] || dbg[0] == '0') {
        return;
    }
    const QString src = ThumtooCache::lastPixelSourceLabel(item->path());
    if (!src.isEmpty()) {
        *text += tr(" · via %1").arg(src);
    }
    const QString q = ThumtooCache::queueStatsLabel();
    if (!q.isEmpty()) {
        *text += tr(" · %1").arg(q);
    }
}

QString ImageView::statusTextEmpty() const
{
    const QString errName = m_session.identity().hasLastLoadError()
        ? PagePath::displayName(m_session.identity().lastLoadErrorRef())
        : QString();
    return HudModel::emptyCanvasStatus(
        m_session.identity().hasLastLoadError(), errName,
        m_image.hasClassicPath(), isImageMode(), isGalleryMode(), isWorkspaceMode());
}

QString ImageView::statusTextMultiItem(ImageItem *item, const QString &quality,
                                       int edge, const QSize &native) const
{
    QString text = HudModel::multiItemHeader(
        isGalleryMode(), m_items.size(), qRound(viewScale() * 100));
    if (!quality.isEmpty()) {
        if (edge > 0) {
            text += tr(" · %1 (%2px)").arg(quality).arg(edge);
        } else {
            text += tr(" · %1").arg(quality);
        }
    }
    if (native.width() > 1 && native.height() > 1
        && native != QSize(1000, 1000) && native != QSize(1024, 1024)) {
        text += tr(" · %1×%2").arg(native.width()).arg(native.height());
    }
    if (isGalleryMode()) {
        int blank = 0, lqip = 0, soft = 0, better = 0, climb = 0;
        for (ImageItem *ii : m_items) {
            if (!ii || ii->path().isEmpty()) {
                continue;
            }
            const int e = ii->displayPixelLongEdge();
            if (!ii->hasDisplayPixels() || e <= 0) {
                ++blank;
            } else if (e <= DisplayQuality::kLqipMaxEdge) {
                ++lqip;
            } else if (e <= DisplayQuality::kSoftMaxEdge) {
                ++soft;
            } else {
                ++better;
            }
            if (const GalleryDecodeState *sit = hostGalleryDecodeBook().get(ii->path())) {
                if (sit->inflight > 0) {
                    ++climb;
                }
            }
        }
        // Pipeline mix is debug-only — never put "LQIP" in the status bar.
        const char *dbg = std::getenv("THUMTOO_DEBUG");
        if (dbg && dbg[0] && dbg[0] != '0') {
            text += HudModel::galleryDebugPixelMixSuffix(blank, lqip, soft, better, climb);
        }
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr(" · Loading %1…").arg(pending);
        }
    } else {
        const int pending = pendingDecodeCount();
        if (pending > 0) {
            text += tr(" · Loading %1…").arg(pending);
        }
    }
    {
        const QString load = ThumtooCache::loadingBreakdownLabel();
        if (!load.isEmpty()) {
            text += tr(" · %1").arg(load);
        }
    }
    if (isWorkspaceMode() && item->isSelected()) {
        const ItemComponents::Placement pl = item->placement();
        text += HudModel::workspaceSelectedItemScaleSuffix(pl.scale, pl.scaleY, pl.rotation);
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}


QString ImageView::statusTextImageMode(ImageItem *item, const QString &quality,
                                       int edge, const QSize &native) const
{
    QString text = tr("%1×%2 · Zoom %3%")
                       .arg(native.width())
                       .arg(native.height())
                       .arg(qRound(viewScale() * 100));
    if (!quality.isEmpty()) {
        // quality may already include "show Npx · native Mpx" — avoid double edge.
        if (edge > 0 && !item->hasDecodedPixels() && !quality.contains(QLatin1String("px"))) {
            text += tr(" · %1 (%2px)").arg(quality).arg(edge);
        } else {
            text += tr(" · %1").arg(quality);
        }
    }
    const QString climb = m_displayPipeline->imageModeClimbActivityLabel(item);
    if (!climb.isEmpty()) {
        text += tr(" · %1").arg(climb);
    }
    {
        const ItemComponents::Placement pl = item->placement();
        text += HudModel::placementFlipRotationSuffix(pl.rotation, pl.hFlip, pl.vFlip);
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}

QString ImageView::statusText() const
{
    if (m_image.zoomRegion().isActive()) {
        return tr("Zoom region: drag a rectangle · Esc cancels");
    }
    ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    if (!item) {
        return statusTextEmpty();
    }

    const QString quality = pixelQualityLabel(item);
    const int edge = item->displayPixelLongEdge();
    const QSize native = item->imageSize();

    if (isMultiItemMode()) {
        return statusTextMultiItem(item, quality, edge, native);
    }
    return statusTextImageMode(item, quality, edge, native);
}

// Phase 6 Tier 1b: public slideshow API forwards to SlideshowController

