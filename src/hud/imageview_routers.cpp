// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView status/HUD routers co-located with hud/ ownership.

#include "imageview.h"
#include "imageitem.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "hud/hudmodel.h"
#include "item/itemcomponents.h"

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
QString ImageView::loadingStatusHudLine() const
{
    // Dedicated HUD line: job queue + cache vs file/archive + weak tiles.
    QString core = ThumtooCache::loadingBreakdownLabel();
    int blank = 0;
    int weak = 0;
    if (isGalleryMode()) {
        m_gallery.countLoadingTileStats(&blank, &weak);
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
        return HudModel::fileNameWithModifiedSuffix(
            PagePath::displayName(item->path()), hostImage().targetHasContentAppearance());
    }
    if (m_image.hasClassicPath() && isImageMode()) {
        return PagePath::displayName(m_image.classicPath());
    }
    return {};
}

QString ImageView::pixelQualityLabel(const ImageItem *item) const
{
    return m_displayPipeline->pixelQualityLabel(item);
}

void ImageView::appendThumtooDebugStatus(QString *text, ImageItem *item) const
{
    if (!text || !item || !HudModel::isThumtooDebugEnabled()) {
        return;
    }
    // "via file decode" / ladder provenance is pipeline debug, not live status.
    // Showing it next to a stuck "Improving quality…" looked like an active decode.
    *text += HudModel::thumtooDebugStatusSuffix(
        ThumtooCache::lastPixelSourceLabel(item->path()),
        ThumtooCache::queueStatsLabel());
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
    int blank = 0, lqip = 0, soft = 0, better = 0, climb = 0;
    const bool galleryDbg = isGalleryMode() && HudModel::isThumtooDebugEnabled();
    if (galleryDbg) {
        m_gallery.countDebugPixelMix(&blank, &lqip, &soft, &better, &climb);
    }
    qreal scaleX = 1.0, scaleY = 1.0, rot = 0.0;
    const bool wsSel = isWorkspaceMode() && item->isSelected();
    if (wsSel) {
        const ItemComponents::Placement pl = item->placement();
        scaleX = pl.scale;
        scaleY = pl.scaleY;
        rot = pl.rotation;
    }
    QString dbg;
    appendThumtooDebugStatus(&dbg, item);
    return HudModel::formatMultiItemStatusLine(
        isGalleryMode(), itemCount(), qRound(viewScale() * 100),
        quality, edge, native,
        galleryDbg, blank, lqip, soft, better, climb,
        pendingDecodeCount(), ThumtooCache::loadingBreakdownLabel(),
        wsSel, scaleX, scaleY, rot,
        hostImage().targetHasContentAppearance(), dbg);
}



QString ImageView::statusTextImageMode(ImageItem *item, const QString &quality,
                                       int edge, const QSize &native) const
{
    const ItemComponents::Placement pl = item->placement();
    QString dbg;
    appendThumtooDebugStatus(&dbg, item);
    return HudModel::formatImageModeStatusLine(
        native.width(), native.height(), qRound(viewScale() * 100),
        quality, edge,
        HudModel::shouldAppendQualityEdgePx(edge, item->hasDecodedPixels(), quality),
        m_displayPipeline->imageModeClimbActivityLabel(item),
        pl.rotation, pl.hFlip, pl.vFlip,
        hostImage().targetHasContentAppearance(), dbg);
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

