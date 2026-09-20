// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "imageitem.h"
#include "imagecache.h"
#include "thumtoocache.h"
#include "displayquality.h"
#include "pathrasterservice.h"
#include "archivepath.h"
#include "pagepath.h"
#include "hudmodel.h"

#include <QFileInfo>
#include <QFontMetrics>
#include "contentxform.h"

void ImageView::refreshStatus()
{
    // Coalesce rapid soft-climb / provenance updates so the HUD and status
    // bar are not rewritten every frame.
    if (!m_statusRefreshTimer) {
        m_statusRefreshTimer = new QTimer(this);
        m_statusRefreshTimer->setSingleShot(true);
        m_statusRefreshTimer->setInterval(HudAppearance::kStatusRefreshMs);
        connect(m_statusRefreshTimer, &QTimer::timeout, this, [this]() {
            emit statusChanged();
            if ((m_hudPrefs.isVisible() || m_hudFlash.isVisible() || m_slideshow.hud().isPausedHud())
                && viewport()) {
                viewport()->update();
            }
        });
    }
    m_statusRefreshTimer->start();
}

void ImageView::setHudVisible(bool on)
{
    if (!m_hudPrefs.setVisible(on)) {
        return;
    }
    // Progress line only paints with the pinned HUD; drive the timer accordingly.
    if (m_slideshow.progressTimer()) {
        if (on && m_slideshow.hud().isProgressActive() && m_slideshow.hud().hasProgressInterval()) {
            m_slideshow.progressTimer()->start();
        } else {
            m_slideshow.progressTimer()->stop();
        }
    }
    viewport()->update();
}

void ImageView::setHudFontPointSize(int pt)
{
    if (!m_hudPrefs.setFontPointSize(pt)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudTextColor(const QColor &color)
{
    if (!m_hudPrefs.setTextColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::setHudPanelColor(const QColor &color)
{
    if (!m_hudPrefs.setPanelColor(color)) {
        return;
    }
    viewport()->update();
}

void ImageView::flashHud(const QString &action, const QString &detail)
{
    m_hudFlash.show(action, detail);
    if (m_hudFlashTimer) {
        m_hudFlashTimer->start(HudFlash::kActionFlashMs);
    }
    viewport()->update();
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
    QStringList extra;
    if (blank > 0) {
        extra << tr("%1 blank").arg(blank);
    }
    if (weak > 0) {
        extra << tr("%1 quick preview").arg(weak);
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
    if (m_sessionId.hasLastLoadError()) {
        return PagePath::displayName(m_sessionId.lastLoadErrorRef());
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
    if (hasClassicPath() && isImageMode()) {
        return PagePath::displayName(classicPath());
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
        galleryNeed = m_displayPipeline.galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        const GallerySoftState *st = m_gallerySoftBook.get(item->path());
        galleryHave = st ? GallerySoft::maxHave(st->have, edge) : edge;
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
    const QString errName = m_sessionId.hasLastLoadError()
        ? PagePath::displayName(m_sessionId.lastLoadErrorRef())
        : QString();
    return HudModel::emptyCanvasStatus(
        m_sessionId.hasLastLoadError(), errName,
        hasClassicPath(), isImageMode(), isGalleryMode(), isWorkspaceMode());
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
            if (const GallerySoftState *sit = m_gallerySoftBook.get(ii->path())) {
                if (sit->inflight > 0) {
                    ++climb;
                }
            }
        }
        // Pipeline mix is debug-only — never put "LQIP" in the status bar.
        const char *dbg = std::getenv("THUMTOO_DEBUG");
        if (dbg && dbg[0] && dbg[0] != '0') {
            text += tr(" · %1 blank · %2 lqip · %3 soft · %4 higher")
                        .arg(blank)
                        .arg(lqip)
                        .arg(soft)
                        .arg(better);
            if (climb > 0) {
                text += tr(" · climbing %1").arg(climb);
            }
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
        if (qAbs(item->itemScaleX() - item->itemScaleY()) < 0.005) {
            text += tr(" · Item %1% · Rot %2°")
                        .arg(qRound(item->itemScaleX() * 100))
                        .arg(qRound(item->itemRotation()));
        } else {
            text += tr(" · Item %1%×%2% · Rot %3°")
                        .arg(qRound(item->itemScaleX() * 100))
                        .arg(qRound(item->itemScaleY() * 100))
                        .arg(qRound(item->itemRotation()));
        }
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}

QString ImageView::imageModeClimbActivityLabel(const ImageItem *item) const
{
    // User-visible activity while samples climb toward *on-screen need*.
    // Do not require native coverage — after progressive Soft→Prefer settle at
    // window size, decoder is idle; claiming "Improving…" was a stuck HUD lie.
    if (!item || item->path().isEmpty()) {
        return {};
    }
    const QString path = item->path();
    const int have = item->displayPixelLongEdge();
    if (have <= 0) {
        return tr("Loading…");
    }
    const int need = m_displayPipeline.imageModeOnScreenNeedEdge();
    // Strict cover (DisplayEdgePolicy::coversEdge): have >= need.
    if (need > 0 && have >= need) {
        return {};
    }
    if (m_displayPipeline.sampleCoversNativeLogical(path, item->displayImage())) {
        return {};
    }
    if (m_pathRaster && m_pathRaster->isClimbPending(path)) {
        return m_pathRaster->isGaveUp(path) ? tr("Decoding full…")
                                            : tr("Improving quality…");
    }
    if (ThumtooCache::isAvailable()) {
        const int want = m_displayPipeline.cappedDisplayEdgeForPath(path, need);
        if (ThumtooCache::isPixelsPending(path, want)
            || ThumtooCache::isPixelsPending(path, ThumtooCache::kGalleryLadderEdge)
            || ThumtooCache::isPixelsPending(path, ThumtooCache::kBatchOverviewEdge)) {
            return tr("Improving quality…");
        }
    }
    // No climb / decode pending: stay quiet even if below native.
    return {};
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
    const QString climb = imageModeClimbActivityLabel(item);
    if (!climb.isEmpty()) {
        text += tr(" · %1").arg(climb);
    }
    if (qAbs(item->itemRotation()) > 0.5) {
        text += tr(" · Rot %1°").arg(qRound(item->itemRotation()));
    }
    if (item->itemHFlip() || item->itemVFlip()) {
        QStringList flips;
        if (item->itemHFlip()) {
            flips << tr("H");
        }
        if (item->itemVFlip()) {
            flips << tr("V");
        }
        text += tr(" · Flip %1").arg(flips.join(QLatin1Char('+')));
    }
    if (targetHasContentAppearance()) {
        text += tr(" · Edited");
    }
    appendThumtooDebugStatus(&text, item);
    return text;
}

QString ImageView::statusText() const
{
    if (m_zoomRegion.isActive()) {
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

