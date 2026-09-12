// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

#include "archivepath.h"
#include "imagecache.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "sessionappearance.h"
#include "thumtoocache.h"
#include "biltoo_thread.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QtMath>

#include <cstdio>
#include <cstdarg>
#include <cstdlib>

namespace {

/** Timestamped load debug (THUMTOO_DEBUG or BILTOO_LOAD_DEBUG). */
bool biltooLoadDebugEnabled()
{
    static const bool on = []() {
        auto env = [](const char *k) {
            const char *e = std::getenv(k);
            return e && e[0] && e[0] != '0';
        };
        return env("THUMTOO_DEBUG") || env("BILTOO_LOAD_DEBUG")
            || env("BILTOO_THUMTOO_DEBUG");
    }();
    return on;
}

void biltooLoadDbg(const char *fmt, ...)
{
    if (!biltooLoadDebugEnabled()) {
        return;
    }
    const qint64 ms = QDateTime::currentMSecsSinceEpoch();
    fprintf(stderr, "biltoo/load t=%lld gui=%d ", static_cast<long long>(ms),
            QThread::isMainThread() ? 1 : 0);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/** ~90% of target long edge counts as delivered (matches soft/overview stops). */
constexpr int kCoverNumer = 9;
constexpr int kCoverDenom = 10;

bool coversEdge(int haveLongEdge, int targetEdge)
{
    return targetEdge <= 0
        || haveLongEdge >= (targetEdge * kCoverNumer) / kCoverDenom;
}

/**
 * Soft / overview climb only (≤ ovCap). Display PreferCache (> overview) is
 * handled by the caller — it needs path + host callbacks.
 */
struct SoftClimbPlan {
    enum class Kind { None, Soft, Overview };
    Kind kind = Kind::None;
    int edge = 0;
};

SoftClimbPlan planSoftClimb(int have, int want, int softCap, int ovCap)
{
    if (want <= 0 || coversEdge(have, want)) {
        return {};
    }
    if (!coversEdge(have, softCap)) {
        const int target = qMin(want, softCap);
        SoftClimbPlan plan{SoftClimbPlan::Kind::Soft, target};
        // Intermediate (e.g. 256 before 512) only when we already have some
        // pixels. Cold have=0 + want=512 used to queue 256 for every visible
        // tile (log: need=512 have=0 req=256) and delay the soft max.
        const int intermediate = ThumtooCache::prevLadderEdge(target);
        if (have > 0 && intermediate > 0 && !coversEdge(have, intermediate)) {
            plan.edge = intermediate;
        }
        return plan;
    }
    if (want > softCap) {
        const int ovTarget = qMin(want, ovCap);
        if (!coversEdge(have, ovTarget)) {
            return {SoftClimbPlan::Kind::Overview, ovTarget};
        }
    }
    return {};
}

/**
 * Gallery soft paint budget: shrink attached soft when the cell needs far less
 * than the sample. Full sample remains in ImageCache for zoom-in.
 */
QImage clampSoftForGalleryCell(const QImage &pixels, int needEdge, int minEdge)
{
    const int have = ImageCache::longEdge(pixels);
    if (needEdge <= 0 || have <= needEdge * 2) {
        return pixels;
    }
    const int target = qMax(needEdge, minEdge);
    if (have <= target) {
        return pixels;
    }
    return ImageCache::clampToMaxEdge(pixels, target);
}

/**
 * Worker-side: bake durable content appearance and clamp for display install.
 * Must not run on the GUI — materialize + scale of multi-MP samples is the
 * ←/→ hitch when done in installDisplayPixels.
 */
QImage prepareImageModeDisplaySample(const QString &path, QImage raw,
                                     SessionAppearance::PixelKind kind)
{
    ASSERT_NOT_GUI_THREAD();
    if (raw.isNull()) {
        return {};
    }
    WorkspaceItemState app;
    ThumtooCache::StoredContentAppearance stored;
    if (ThumtooCache::loadContentAppearance(path, &stored)) {
        app.contentHFlip = stored.contentHFlip;
        app.contentVFlip = stored.contentVFlip;
        app.contentQuarterTurns = stored.contentQuarterTurns;
        app.hasCrop = stored.hasCrop;
        app.cropRect = stored.cropRect;
        app.cropSourceSize = stored.cropSourceSize;
        app.cropRotation = stored.cropRotation;
        if (stored.hasGrade) {
            app.colorAdjust.brightness = stored.gradeBrightness;
            app.colorAdjust.contrast =
                stored.gradeContrast == 0 ? 100 : stored.gradeContrast;
            app.colorAdjust.saturation =
                stored.gradeSaturation == 0 ? 100 : stored.gradeSaturation;
            app.colorAdjust.hue = stored.gradeHue;
            app.colorAdjust.gamma = stored.gradeGamma <= 0
                ? 1.0
                : (stored.gradeGamma / 100.0);
            app.colorAdjust.invert = stored.gradeInvert;
        }
    }
    if (SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity()) {
        raw = SessionAppearance::materializeDisplay(raw, app, kind);
    }
    const int cap = (kind == SessionAppearance::PixelKind::SoftPreview)
                        ? ThumtooCache::kGalleryLadderEdge
                        : ThumtooCache::kImageLadderEdge;
    if (ImageCache::longEdge(raw) > cap) {
        raw = raw.scaled(cap, cap, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    return raw;
}

/** Queue onImagePreviewLoaded on the GUI thread; no-op if @a guard is gone. */
void queuePreviewLoaded(const QPointer<ImageView> &guard, const QString &path,
                        const QImage &preview, quint64 gen, int role)
{
    if (!guard || preview.isNull()) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, preview, gen, role]() {
        if (!guard) {
            return;
        }
        guard->onImagePreviewLoaded(path, preview, gen, role);
    });
}

/** Queue onImageLoaded on the GUI thread; no-op if @a guard is gone. */
void queueImageLoaded(const QPointer<ImageView> &guard, const QString &path,
                      const QImage &image, quint64 gen, int role)
{
    if (!guard) {
        return;
    }
    QTimer::singleShot(0, guard.data(), [guard, path, image, gen, role]() {
        if (!guard) {
            return;
        }
        guard->onImageLoaded(path, image, gen, role);
    });
}

/**
 * Host soft first (any size), then loadThumbnail, then LQIP.
 * Never drop a smaller host soft when the requested edge is not ready yet —
 * that left Image mode / slideshow blank until the high-res job finished.
 */
QImage loadSoftPreviewPixels(const QString &path, int softEdge)
{
    ASSERT_NOT_GUI_THREAD();
    // Prefer an adequate host sample; otherwise keep any smaller host soft.
    QImage preview = ImageCache::get(path, softEdge);
    if (preview.isNull()) {
        preview = ImageCache::get(path);
    }
    if (ImageCache::adequate(preview, softEdge)) {
        return preview;
    }
    const QImage loaded = ImageLoader::loadThumbnail(path, softEdge);
    if (!loaded.isNull()
        && ImageCache::longEdge(loaded) >= ImageCache::longEdge(preview)) {
        preview = loaded;
    }
    if (preview.isNull()) {
        preview = ThumtooCache::cachedLqipImage(path);
        if (!preview.isNull()) {
            ImageCache::put(path, preview);
        }
    }
    return preview;
}

/**
 * High-priority pool job: soft stand-in for rapid next/prev.
 * Generation-checked so a newer LoadReplace cancels a stale preview.
 */
void startSoftPreviewJob(const QPointer<ImageView> &guard, const QString &path,
                         quint64 gen, int roleInt, int softEdge)
{
    biltooLoadDbg("softJob START path=%s edge=%d gen=%llu",
                  qPrintable(QFileInfo(path).fileName()), softEdge,
                  static_cast<unsigned long long>(gen));
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, softEdge]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                biltooLoadDbg("softJob STALE path=%s gen=%llu",
                              qPrintable(QFileInfo(path).fileName()),
                              static_cast<unsigned long long>(gen));
                return;
            }
            QImage preview = loadSoftPreviewPixels(path, softEdge);
            biltooLoadDbg("softJob DONE path=%s got=%dx%d",
                          qPrintable(QFileInfo(path).fileName()),
                          preview.width(), preview.height());
            if (!preview.isNull()) {
                preview = prepareImageModeDisplaySample(
                    path, preview, SessionAppearance::PixelKind::SoftPreview);
            }
            queuePreviewLoaded(guard, path, preview, gen, roleInt);
        },
        2);
}

/**
 * Low-priority pool job: PreferCache / loadThumbnail at a display edge
 * (slideshow quality climb). Schedules PreferCache on miss.
 */
void startDisplayQualityJob(const QPointer<ImageView> &guard, const QString &path,
                            quint64 gen, int roleInt, int qualityEdge)
{
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, qualityEdge]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                return;
            }
            // Keep any host sample; do not require qualityEdge up front or a
            // smaller soft is discarded and the UI waits on high-res only.
            QImage image = ImageCache::get(path, qualityEdge);
            if (image.isNull()) {
                image = ImageCache::get(path);
            }
            if (!ImageCache::adequate(image, qualityEdge)) {
                const QImage loaded = ImageLoader::loadThumbnail(path, qualityEdge);
                if (!loaded.isNull()
                    && ImageCache::longEdge(loaded) >= ImageCache::longEdge(image)) {
                    image = loaded;
                }
            }
            if (!guard) {
                return;
            }
            if (image.isNull()) {
                if (ThumtooCache::isAvailable()) {
                    (void)ThumtooCache::scheduleDisplayPixels(path, qualityEdge);
                }
                return;
            }
            // Soft stand-in still upgrades the view; keep climbing via schedule.
            if (!ImageCache::adequate(image, qualityEdge) && ThumtooCache::isAvailable()) {
                (void)ThumtooCache::scheduleDisplayPixels(path, qualityEdge);
            }
            if (!image.isNull()) {
                image = prepareImageModeDisplaySample(
                    path, image, SessionAppearance::PixelKind::FullSource);
            }
            if (ImageCache::longEdge(image) > qualityEdge) {
                image = image.scaled(qualityEdge, qualityEdge, Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
            }
            queueImageLoaded(guard, path, image, gen, roleInt);
        },
        -1);
}

} // namespace

int ImageView::pathOrderOccurrences(const QString &path) const
{
    int n = 0;
    for (const QString &p : m_pathOrder) {
        if (p == path) {
            ++n;
        }
    }
    return n;
}

WorkspaceItemState ImageView::appearanceForNewImageModeItem(const QString &path)
{
    // Prefer stable session-image id appearance; path map is legacy only.
    //
    // Image mode LoadReplace: the sole canvas item is the current session
    // image, so m_currentSessionId identifies it correctly.
    //
    // Gallery / Workspace LoadAdd must not call this: each tile is bound to
    // its own session id *after* creation. Applying m_currentSessionId here
    // would bake the navigated image's crop into every newly decoded tile.
    if (m_currentSessionId != kInvalidSessionImageId) {
        seedSessionAppearanceFromState(m_currentSessionId, path);
        if (const WorkspaceItemState *sit = m_appearance.get(m_currentSessionId)) {
            return *sit;
        }
        // Bound session image with no appearance entry = full frame, no path fallback.
        return {};
    }
    // Path map only when unbound (no session image id).
    const auto it = m_itemStates.constFind(path);
    if (it != m_itemStates.cend()) {
        return *it;
    }
    return {};
}

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    // Size-first ctor + setSourceImageReady: never QPixmap::fromImage of multi-MP
    // in ImageItem(path, image) during LoadReplace.
    const QSize layout = layoutSizeForPath(path, image);
    const QSize intrinsic = (layout.width() > 1 && layout.height() > 1)
                                ? layout
                                : QSize(1, 1);
    auto *item = new ImageItem(path, intrinsic);
    if (isImageMode()) {
        item->setSourceImageReady(image);
    } else {
        item->setSourceImage(image);
    }
    applyItemModeFlags(item);
    // Session crop survives navigation. Image mode LoadReplace jobs already
    // bake durable appearance on the worker — only sync chrome flags here.
    // applyContentToItem would materialize + fromImage again on the GUI.
    if (applyStoredSessionCrop && isImageMode()) {
        const bool haveId = m_currentSessionId != kInvalidSessionImageId;
        const bool havePath = m_itemStates.contains(path);
        if (haveId || havePath) {
            const WorkspaceItemState app = appearanceForNewImageModeItem(path);
            if (haveId && !m_appearance.get(m_currentSessionId)) {
                // no store entry
            } else {
                item->setContentHFlip(app.contentHFlip);
                item->setContentVFlip(app.contentVFlip);
                item->setSessionCrop(app.hasCrop, app.cropRect);
                item->setColorAdjustmentsRecord(app.colorAdjust);
                SessionAppearance::syncItemLayoutToContentOrientation(item, app);
            }
        }
    }
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}


void ImageView::seedSessionAppearanceFromState(SessionImageId sid, const QString &path)
{
    if (sid == kInvalidSessionImageId || path.isEmpty()) {
        return;
    }
    if (m_appearance.contains(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (const WorkspaceItemState *cur = m_appearance.get(sid)) {
            if (SessionAppearance::hasContentAppearance(*cur)) {
                return;
            }
        }
    }
    ThumtooCache::StoredContentAppearance stored;
    if (!ThumtooCache::loadContentAppearance(path, &stored)) {
        return;
    }
    WorkspaceItemState seed;
    seed.sessionId = sid;
    seed.path = path;
    seed.contentHFlip = stored.contentHFlip;
    seed.contentVFlip = stored.contentVFlip;
    seed.contentQuarterTurns = stored.contentQuarterTurns;
    seed.hasCrop = stored.hasCrop;
    seed.cropRect = stored.cropRect;
    seed.cropSourceSize = stored.cropSourceSize;
    seed.cropRotation = stored.cropRotation;
    if (stored.hasGrade) {
        seed.colorAdjust.brightness = stored.gradeBrightness;
        seed.colorAdjust.contrast =
            stored.gradeContrast == 0 ? 100 : stored.gradeContrast;
        seed.colorAdjust.saturation =
            stored.gradeSaturation == 0 ? 100 : stored.gradeSaturation;
        seed.colorAdjust.hue = stored.gradeHue;
        // Durable gradeGamma is percent (100 = 1.0); treat 0 as identity.
        seed.colorAdjust.gamma = stored.gradeGamma <= 0
            ? 1.0
            : (stored.gradeGamma / 100.0);
        seed.colorAdjust.invert = stored.gradeInvert;
    }
    m_appearance.set(sid, seed);
}

void ImageView::installDisplayPreservingView(ImageItem *item, const QImage &pixels,
                                             SessionAppearance::PixelKind kind,
                                             SessionImageId sid)
{
    if (!item || pixels.isNull()) {
        return;
    }
    const QSize before = item->imageSize();
    if (sid == kInvalidSessionImageId) {
        sid = item->sessionId() != kInvalidSessionImageId
                  ? item->sessionId()
                  : m_currentSessionId;
    }
    installDisplayPixels(item, pixels, kind, sid);
    preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
}

bool ImageView::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    // Single gate for soft→HQ and against late soft demoting full.
    if (!item || pixels.isNull()) {
        return false;
    }
    const int incoming = ImageCache::longEdge(pixels);
    if (incoming <= 0) {
        return false;
    }
    // Never replace full (non-preview) pixels with a SoftPreview sample.
    if (kind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        return false;
    }
    // Equal-or-smaller sample is not an upgrade (blank tiles still accept).
    if (item->hasDisplayPixels() && !item->shouldUpgradeDisplayTo(incoming)) {
        return false;
    }
    return true;
}

void ImageView::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    if (!canAcceptDisplaySample(item, pixels, kind)) {
        return;
    }
    const QSize layoutBefore = item->imageSize();
    const QString path = item->path();

    // Raw samples → unified host cache (slideshow/gallery reuse).
    if (!path.isEmpty()) {
        ImageCache::put(path, pixels);
    }

    // Resolve session id (Image-mode soft path often passes invalid sid).
    if (sid == kInvalidSessionImageId) {
        if (item->sessionId() != kInvalidSessionImageId) {
            sid = item->sessionId();
        } else if (isImageMode() && m_currentSessionId != kInvalidSessionImageId) {
            sid = m_currentSessionId;
        }
    }
    seedSessionAppearanceFromState(sid, path);

    WorkspaceItemState appearance;
    if (sid != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = m_appearance.get(sid)) {
            appearance = *app;
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        // Unbound tile only: path map is legacy fallback (IDENTITY.md).
        const auto it = m_itemStates.constFind(path);
        if (it != m_itemStates.cend()) {
            appearance = *it;
        }
    }

    // raw → optional gallery soft clamp → materializeDisplay → attach.
    QImage pixelsForDisplay = pixels;
    if (isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        pixelsForDisplay = clampSoftForGalleryCell(
            pixels,
            galleryDisplayEdgeForItem(item, /*allowHighRes=*/true),
            ThumtooCache::kFilmstripLadderEdge);
    }
    // Image mode LoadReplace jobs bake appearance on the worker. Gallery still
    // materializes here (soft ≤512). Never DeviceCoordinateCache on install —
    // that snapshots multi-MP on the GUI every ←/→.
    QImage display = pixelsForDisplay;
    const bool imageModeInstall = isImageMode();
    if (!imageModeInstall) {
        display = SessionAppearance::materializeDisplay(pixelsForDisplay, appearance, kind);
    } else if (ImageCache::longEdge(pixelsForDisplay) <= ThumtooCache::kGalleryLadderEdge
               && (SessionAppearance::hasContentAppearance(appearance)
                   || !appearance.colorAdjust.isIdentity())) {
        // Soft/LQIP pending path may still need a cheap bake on GUI.
        display = SessionAppearance::materializeDisplay(pixelsForDisplay, appearance, kind);
    }

    if (kind == SessionAppearance::PixelKind::FullSource) {
        if (imageModeInstall) {
            item->setSourceImageReady(display);
        } else {
            item->setSourceImage(display);
        }
        // Logical size from path — FullSource may be a ladder step, not geometry.
        QSize logical = logicalSizeForPath(path);
        if (!isPositiveSize(logical) || logical.width() <= 1 || logical.height() <= 1
            || isProvisionalImageSize(path)) {
            logical = layoutSizeForPath(path, display);
        }
        if (isPositiveSize(logical) && logical.width() > 1 && logical.height() > 1) {
            item->setIntrinsicSize(logical);
        }
        item->setCacheMode(QGraphicsItem::NoCache);
        item->update();
    } else {
        item->setPreviewImage(display); // NoCache soft path
        // Soft must not leave intrinsic at 1×1 after a size-less placeholder.
        const QSize cur = item->imageSize();
        if (cur.width() <= 1 || cur.height() <= 1) {
            const QSize layout = layoutSizeForPath(path, display);
            if (isPositiveSize(layout) && layout.width() > 1 && layout.height() > 1) {
                item->setIntrinsicSize(layout);
            }
        }
    }
    item->setContentHFlip(appearance.contentHFlip);
    item->setContentVFlip(appearance.contentVFlip);
    item->setSessionCrop(appearance.hasCrop, appearance.cropRect);
    // Baked samples: record grade for HUD only — do not rebuild the pixmap.
    item->setColorAdjustmentsRecord(appearance.colorAdjust);
    SessionAppearance::syncItemLayoutToContentOrientation(item, appearance);

    // Do NOT emit sessionAppearanceChanged from decode/install (filmstrip is
    // selection-coupled). Soft ladder upgrades must not rewrite the strip.

    // Gallery reflow only when layout geometry actually changed.
    if (isGalleryMode() && item->imageSize() != layoutBefore) {
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    auto *item = new ImageItem(path, intrinsicSize);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    return item;
}

void ImageView::bindImageModeSessionCursor(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Image-mode crop/flip targets the matching Workspace session slot.
    if (m_currentSessionId != kInvalidSessionImageId) {
        item->setSessionId(m_currentSessionId);
    }
    if (m_sessionIndex >= 0) {
        item->setSessionIndex(m_sessionIndex);
    }
}

void ImageView::resetImageModeItemPlacement(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Never inherit Gallery/Workspace free-form placement or scale.
    item->setInteractive(false);
    item->setScaleHandlesEnabled(false);
    item->setItemScale(1.0);
    item->setPos(0, 0);
    item->setItemRotation(0.0);
}

QImage ImageView::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview) const
{
    // Prefer explicit preview, then best ImageCache sample (not filmstrip-only
    // 256 when 512+ is already cached), then LQIP. GUI-safe — no loadThumbnail.
    if (!preview.isNull()) {
        return preview;
    }
    QImage pixels = slideshowRaster(path);
    if (pixels.isNull()) {
        // Prefer soft ladder (≥512) when present; fall back to any cache hit.
        pixels = ImageCache::get(path, ThumtooCache::kGalleryLadderEdge);
    }
    if (pixels.isNull()) {
        pixels = ImageCache::get(path);
    }
    if (pixels.isNull()) {
        pixels = ThumtooCache::cachedLqipImage(path);
        if (!pixels.isNull()) {
            ImageCache::put(path, pixels);
        }
    }
    return pixels;
}

void ImageView::installImageModePendingTile(const QString &path, const QImage &preview)
{
    if (!isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the viewport with dwell/live blits. Pending tile used to
    // clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed. Underlay is hidden for the whole show.
    if (m_slideshowProgressActive) {
        return;
    }

    const QImage pixels = resolveImageModePendingPixels(path, preview);
    biltooLoadDbg("pendingTile path=%s soft=%dx%d cache=%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height(),
                  ImageCache::has(path) ? 1 : 0);

    // Cold path: no soft/LQIP yet. Do NOT rebuild the scene / fitInView here —
    // timed logs showed ~100ms+ GUI between pendingTile soft=0x0 and softJob
    // START. Keep prior frame (or empty placeholder) until soft arrives.
    if (pixels.isNull()) {
        if (m_items.size() == 1) {
            ImageItem *item = m_items.first();
            item->setPath(path);
            bindImageModeSessionCursor(item);
            biltooLoadDbg("pendingTile DEFER empty soft path=%s keep prior frame",
                          qPrintable(QFileInfo(path).fileName()));
            return;
        }
        // First image ever: minimal placeholder, no fit storm.
        const QSize sz = layoutSizeForPath(path, QImage());
        ImageItem *item = createPlaceholderItem(path, sz);
        if (item) {
            bindImageModeSessionCursor(item);
            resetImageModeItemPlacement(item);
            prepareImageModeCanvas();
        }
        biltooLoadDbg("pendingTile PLACEHOLDER empty soft path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
    }

    // Layout size = native when known; else preview aspect so fitInView fills
    // the window (not a provisional square that letterboxes the content).
    const QSize sz = layoutSizeForPath(path, pixels);

    setUpdatesEnabled(false);

    // Fast path: reuse the single Image-mode item.
    ImageItem *item = nullptr;
    if (m_items.size() == 1) {
        item = m_items.first();
    }
    if (item) {
        const QSize sizeBefore = item->imageSize();
        item->setPath(path);
        if (sz.width() > 1 && sz.height() > 1) {
            item->setIntrinsicSize(sz);
        }
        bindImageModeSessionCursor(item);
        // Fast soft attach: no installDisplayPixels, no fitItem/fitInView.
        if (!path.isEmpty()) {
            ImageCache::put(path, pixels);
        }
        // MUST clear prior FullSource first. setPreviewImage no-ops when
        // hasDecodedPixels() (designed for "late soft after full" on SAME
        // image). On ←/→ the item still holds the previous image's FullSource
        // → soft never attaches → canAccept rejects ladder upgrades → stuck
        // on old frame or empty (log: ladderReady UPGRADE with no tryInstall OK).
        if (item->hasDecodedPixels()) {
            item->clearDecodedPixels();
        }
        item->setPreviewImage(pixels);
        // Intrinsic from known logical only — never from soft sample dims.
        const QSize known = logicalSizeForPath(path);
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !isProvisionalImageSize(path)) {
            item->setIntrinsicSize(known);
            // Same-aspect scale only. Aspect change: leave matrix; user sees
            // letterboxing until a deferred fit (no fitInView on the key path).
            if (sizeBefore.width() > 1 && sizeBefore.height() > 1) {
                const qreal a0 = double(sizeBefore.width()) / sizeBefore.height();
                const qreal a1 = double(known.width()) / known.height();
                if (qAbs(a0 - a1) <= 0.02 && sizeBefore != known) {
                    preserveImageViewOnLogicalSizeChange(item, sizeBefore, known);
                }
            }
        }
        setUpdatesEnabled(true);
        if (viewport()) {
            viewport()->update();
        }
        biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d fit=0",
                      qPrintable(QFileInfo(path).fileName()),
                      pixels.width(), pixels.height());
        return;
    }

    clearLiveCanvas();
    item = createPlaceholderItem(path, sz);
    if (!item) {
        setUpdatesEnabled(true);
        return;
    }
    bindImageModeSessionCursor(item);
    installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                         m_currentSessionId);
    resetImageModeItemPlacement(item);
    prepareImageModeCanvas();
    fitItem(item, currentFitAspectMode());
    m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    setUpdatesEnabled(true);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
    biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height());
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    if (path.isEmpty()) {
        return;
    }
    if (role == LoadAdd) {
        addPendingWorkspacePath(path);
    }
    // LoadRestore pending is owned by m_pendingRestoreStates (AUDIT M27).
    // AUDIT H3a: only LoadReplace advances the generation token so workspace
    // adds cannot cancel an in-flight Image-mode navigation decode.
    quint64 gen = m_loadGeneration.load();
    if (role == LoadReplace) {
        gen = ++m_loadGeneration;
        m_imageModeNativeClimbPaths.clear();
        m_imageModeClimb.clear();
        // Do NOT setPrimaryInterest here — that starts EnsureTiles / FocusFull
        // pyramid builds on archives and cancels the soft queue every ←/→.
    }
    // LoadReplace: do NOT emit statusChanged — MainWindow finishCurrentIndexChromeUpdate
    // already updateStatus(); a second statusChanged re-entered updateStatus and
    // rebuilt chrome on every ←/→ (statusText, metadata, adjustments, filmstrip pending).

    // Slideshow dual-blit already decoded this path — reuse under the hold.
    if (role == LoadReplace && tryDeliverReplaceFromSlideshowRaster(path, gen)) {
        return;
    }

    // Image mode: paint soft/LQIP from cache immediately (pixel swap only).
    if (role == LoadReplace && isImageMode()) {
        installImageModePendingTile(path);
        // Soft already on the item: only climb PreferCache; skip soft pool job.
        if (ImageItem *it = imageModeItemForPath(path)) {
            if (it->displayPixelLongEdge() > 0) {
                const QImage soft = it->displayImage();
                const QString pathCopy = path;
                QTimer::singleShot(0, this, [this, pathCopy, soft]() {
                    if (!isImageMode() || classicPath() != pathCopy) {
                        return;
                    }
                    ensureImageModeQualityClimb(pathCopy, soft);
                });
                biltooLoadDbg("PATH soft-on-item skip softJob climb deferred path=%s edge=%d",
                              qPrintable(QFileInfo(path).fileName()),
                              it->displayPixelLongEdge());
                return;
            }
        }
    }

    // Cold host: kick soft ladder before pool jobs compete with native full.
    if (role == LoadReplace && ThumtooCache::isAvailable()
        && ImageCache::get(path).isNull()) {
        (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
    }

    if (m_slideshowProgressActive) {
        scheduleSlideshowReplaceDecode(path, gen, role);
        return;
    }
    scheduleClassicImageDecode(path, gen, role);
}

bool ImageView::tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen)
{
    const QImage ready = slideshowRaster(path);
    if (ready.isNull()) {
        return false;
    }
    const QPointer<ImageView> guard(this);
    QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                              Q_ARG(QString, path),
                              Q_ARG(QImage, ready),
                              Q_ARG(quint64, gen),
                              Q_ARG(int, static_cast<int>(LoadReplace)));
    return true;
}

void ImageView::scheduleSlideshowReplaceDecode(const QString &path, quint64 gen,
                                               LoadRole role)
{
    // Soft first (priority), then PreferCache at target edge (low).
    // Key-repeat skips loadImage entirely (MainWindow debounce); this path is
    // for settled index / auto-advance — must climb above soft max or the show
    // stays on thumbnails forever.
    const QPointer<ImageView> guard(this);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const int qualityEdge = slideshowTargetEdge();
    const int roleInt = static_cast<int>(role);

    startSoftPreviewJob(guard, path, gen, roleInt, softEdge);
    if (qualityEdge > softEdge) {
        startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge);
    }
}

void ImageView::scheduleClassicImageDecode(const QString &path, quint64 gen,
                                           LoadRole role)
{
    // Soft only on ←/→. PreferCache climb (capped to on-screen need) is kicked
    // from tryInstall after soft lands. Native ImageLoader::load / EnsureTiles
    // are NOT started here — archive full extract and tile pyramids were the
    // dominant cost in THUMTOO_DEBUG logs on every key.
    const QPointer<ImageView> guard(this);
    const int roleInt = static_cast<int>(role);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;

    startSoftPreviewJob(guard, path, gen, roleInt, softEdge);
    Q_UNUSED(gen);
}


int ImageView::itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes) const
{
    // On-screen long edge in device pixels, snapped to a ladder step.
    // Gallery visible tiles and Image-mode zoom climb share this metric.
    if (!item) {
        return ThumtooCache::kFilmstripLadderEdge;
    }
    const QRectF br = item->contentSceneRect();
    if (br.isEmpty()) {
        return ThumtooCache::kFilmstripLadderEdge;
    }
    const QPointF a = mapFromScene(br.topLeft());
    const QPointF b = mapFromScene(br.bottomRight());
    const qreal longPx =
        qMax(qAbs(b.x() - a.x()), qAbs(b.y() - a.y())) * devicePixelRatioF();
    const int need = ThumtooCache::ceilLadderEdge(int(qCeil(longPx)));
    if (!allowHighRes) {
        return qMin(need, ThumtooCache::kGalleryLadderEdge);
    }
    return need;
}

int ImageView::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    // Visible tiles: full on-screen need. Off-screen / idle: soft band only.
    return itemOnScreenNeedEdge(item, allowHighRes);
}


int ImageView::galleryDecodeConcurrency()
{
    static int n = []() {
        int v = kMaxConcurrentGalleryDecodes;
        if (const char *e = std::getenv("BILTOO_GALLERY_DECODE_CONCURRENCY")) {
            const int parsed = QString::fromLocal8Bit(e).toInt();
            if (parsed >= 1 && parsed <= 32) {
                v = parsed;
            }
        }
        return v;
    }();
    return n;
}

int ImageView::gallerySoftInflightCount() const
{
    int n = 0;
    for (auto it = m_gallerySoft.cbegin(); it != m_gallerySoft.cend(); ++it) {
        if (it.value().inflight > 0 || it.value().fullInflight) {
            ++n;
        }
    }
    return n;
}

void ImageView::gallerySoftResetPath(const QString &path)
{
    m_gallerySoft.remove(path);
}

void ImageView::gallerySoftResetAll()
{
    m_gallerySoft.clear();
}

int ImageView::galleryWantEdgeForPath(const QString &path,
                                      const QRectF &sceneVisible) const
{
    int want = ThumtooCache::kFilmstripLadderEdge;
    bool anyVisible = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        const QRectF tile = item->contentSceneRect();
        if (tile.isNull() || !tile.isValid()) {
            continue;
        }
        const bool visible = sceneVisible.intersects(tile);
        const int edge = galleryDisplayEdgeForItem(item, /*allowHighRes=*/visible);
        want = qMax(want, edge);
        anyVisible = anyVisible || visible;
    }
    if (!anyVisible) {
        // Idle / off-screen: placeholder band only.
        want = qMin(want, ThumtooCache::kGalleryLadderEdge);
    }
    return want;
}

int ImageView::galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const
{
    int have = 0;
    bool anyFull = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        if (item->hasDecodedPixels()) {
            anyFull = true;
        }
        have = qMax(have, item->displayPixelLongEdge());
    }
    if (anyFullOut) {
        *anyFullOut = anyFull;
    }
    return have;
}

bool ImageView::resolveGallerySoftHaveWant(const QString &path, GallerySoftState &st,
                                           int *haveOut, int *wantOut)
{
    // Fast path: updateGalleryDecodeWindow already filled st.have / st.want.
    // Do not re-scan all items here — that was O(n²) on large galleries.
    int have = st.have;
    int want = st.want;
    if (want <= 0) {
        const QRect viewRect = viewport()->rect().adjusted(
            -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
            kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
        const QRectF sceneVisible = mapToScene(viewRect).boundingRect();

        bool anyFull = false;
        have = galleryHaveEdgeFromItems(path, &anyFull);
        st.have = have;
        if (anyFull) {
            return false;
        }
        // Host soft only for direct callers (decode-window pass1 owns installs).
        if (have <= 0) {
            const QImage hostSoft = ImageCache::get(path);
            if (!hostSoft.isNull()) {
                onImagePreviewLoaded(path, hostSoft, m_loadGeneration.load(),
                                     static_cast<int>(LoadAdd));
                have = qMax(have, ImageCache::longEdge(hostSoft));
                st.have = have;
            }
        }
        want = galleryWantEdgeForPath(path, sceneVisible);
        st.want = want;
    }
    if (want <= 0 || have >= want) {
        return false;
    }
    *haveOut = have;
    *wantOut = want;
    return true;
}

void ImageView::scheduleGalleryDisplayPreferCache(const QString &path, GallerySoftState &st,
                                                  int have, int want)
{
    // Overview in hand: PreferCache display raster ≤2048.
    const int dispEdge = qMin(want, ThumtooCache::kImageLadderEdge);
    if (coversEdge(have, dispEdge)) {
        st.gaveUpWant = qMax(st.gaveUpWant, want);
        return;
    }
    if (ThumtooCache::isPixelsPending(path, dispEdge)) {
        markGallerySoftInflight(st, dispEdge);
        return;
    }
    if (ThumtooCache::scheduleDisplayPixels(path, dispEdge)) {
        markGallerySoftInflight(st, dispEdge);
        if (const char *dbg = std::getenv("THUMTOO_DEBUG");
            dbg && dbg[0] != '\0' && dbg[0] != '0') {
            fprintf(stderr,
                    "biltoo/gallery: display request path need=%d have=%d "
                    "disp=%d\n",
                    want, have, dispEdge);
        }
        return;
    }
    const QImage hit = ImageCache::get(path, dispEdge);
    if (!hit.isNull()) {
        const int got = ImageCache::longEdge(hit);
        if (got > have) {
            onImagePreviewLoaded(path, hit, m_loadGeneration.load(),
                                 static_cast<int>(LoadAdd));
            st.have = qMax(st.have, got);
        }
    }
    if (!coversEdge(st.have, dispEdge)) {
        st.gaveUpWant = qMax(st.gaveUpWant, dispEdge);
    }
}

void ImageView::markGallerySoftInflight(GallerySoftState &soft, int edge)
{
    soft.inflight = edge;
    soft.inflightSinceMs = QDateTime::currentMSecsSinceEpoch();
}

void ImageView::clearGallerySoftInflight(GallerySoftState &soft)
{
    soft.inflight = 0;
    soft.inflightSinceMs = 0;
}

int ImageView::installGallerySoftPreview(const QString &path, const QImage &preview,
                                         quint64 gen, GallerySoftState &soft,
                                         const char *debugTag, int requestEdge)
{
    if (preview.isNull()) {
        return 0;
    }
    const int got = ImageCache::longEdge(preview);
    onImagePreviewLoaded(path, preview, gen, static_cast<int>(LoadAdd));
    soft.have = qMax(soft.have, got);
    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        if (requestEdge > 0) {
            fprintf(stderr,
                    "biltoo/gallery: INSTALL soft path=%s got=%d request=%d (%s)\n",
                    qPrintable(QFileInfo(path).fileName()), got, requestEdge,
                    debugTag ? debugTag : "");
        } else {
            fprintf(stderr, "biltoo/gallery: INSTALL soft path=%s got=%d (%s)\n",
                    qPrintable(QFileInfo(path).fileName()), got,
                    debugTag ? debugTag : "");
        }
    }
    return got;
}

void ImageView::hostGallerySoftFromCache(const QString &path, quint64 gen,
                                         GallerySoftState &soft, int edge)
{
    const QImage hit = ImageCache::get(path, edge);
    if (!hit.isNull()) {
        onImagePreviewLoaded(path, hit, gen, static_cast<int>(LoadAdd));
        soft.have = qMax(soft.have, ImageCache::longEdge(hit));
    }
}

void ImageView::retryGallerySoftOrOverview(const QString &path, quint64 gen,
                                           GallerySoftState &soft, int edge,
                                           bool overview)
{
    // PreferCache may already have scheduled SoftOnly (empty image). Do not
    // clear inflight while thumtoo still holds the job — that caused
    // have=0/req=256 soft-request loops with schedulePixels SKIP spam.
    if (ThumtooCache::isPixelsPending(path, edge)) {
        markGallerySoftInflight(soft, edge);
        return;
    }
    const bool queued = overview ? ThumtooCache::scheduleOverviewPixels(path, edge)
                                 : ThumtooCache::schedulePixels(path, edge);
    if (queued) {
        markGallerySoftInflight(soft, edge);
        return;
    }
    // Settled (or unavailable): host from ImageCache; release the slot.
    hostGallerySoftFromCache(path, gen, soft, edge);
    clearGallerySoftInflight(soft);
    if (!coversEdge(soft.have, edge)) {
        soft.gaveUpWant = qMax(soft.gaveUpWant, edge);
    }
}

void ImageView::advanceGallerySoftAfterPool(const QString &path, quint64 gen,
                                            GallerySoftState &soft, int got,
                                            int requestEdge)
{
    // Climb only when below the request edge — and only keep inflight when a
    // host callback is truly pending (schedule* returned true). SKIP must not
    // pin soft.inflight forever.
    if (coversEdge(got, requestEdge)) {
        clearGallerySoftInflight(soft);
        if (soft.gaveUpWant <= requestEdge) {
            soft.gaveUpWant = 0;
        }
        return;
    }
    if (!ThumtooCache::isAvailable()) {
        clearGallerySoftInflight(soft);
        soft.gaveUpWant = qMax(soft.gaveUpWant, requestEdge);
        // Do not set failed=true on first miss — PreferCache / SoftOnly may
        // still deliver; failed is permanent and skips forever.
        return;
    }
    if (requestEdge <= ThumtooCache::kGalleryLadderEdge) {
        retryGallerySoftOrOverview(path, gen, soft, requestEdge, /*overview=*/false);
        return;
    }
    if (requestEdge <= ThumtooCache::kBatchOverviewEdge) {
        const int ov = qMin(requestEdge, ThumtooCache::kBatchOverviewEdge);
        retryGallerySoftOrOverview(path, gen, soft, ov, /*overview=*/true);
        return;
    }
    clearGallerySoftInflight(soft);
    soft.gaveUpWant = qMax(soft.gaveUpWant, requestEdge);
}

void ImageView::applyGallerySoftPoolResult(const QString &path, const QImage &preview,
                                           quint64 gen, int requestEdge)
{
    auto it = m_gallerySoft.find(path);
    if (it == m_gallerySoft.end()) {
        takePendingWorkspacePath(path);
        return;
    }
    GallerySoftState &soft = it.value();
    // Superseded (ladderReady cleared inflight, or a newer edge was requested):
    // still install any soft we decoded — dropping it left blank tiles when
    // SoftOnly finished before this callback.
    if (soft.inflight != requestEdge) {
        takePendingWorkspacePath(path);
        installGallerySoftPreview(path, preview, gen, soft,
                                  "superseded pool callback", /*requestEdge=*/0);
        if (isGalleryMode()) {
            scheduleGalleryDecodeWindowRefresh(48);
        }
        return;
    }
    takePendingWorkspacePath(path);

    const int got =
        installGallerySoftPreview(path, preview, gen, soft, "pool PreferCache",
                                  requestEdge);
    advanceGallerySoftAfterPool(path, gen, soft, got, requestEdge);

    // Coalesce status + decode-window — every INSTALL used to refresh the HUD
    // and rescan the gallery on the GUI thread.
    scheduleGalleryDecodeWindowRefresh(48);
    refreshStatus();
}

void ImageView::clearGalleryGaveUpIfClimbable(GallerySoftState &st, int have, int want)
{
    // Higher zoom/need or soft arrived — clear shortfall and retry climb.
    if (st.gaveUpWant > 0
        && (want > st.gaveUpWant || coversEdge(have, st.gaveUpWant))) {
        st.gaveUpWant = 0;
    }
}

bool ImageView::gallerySoftScheduleBlocked(const GallerySoftState &st, int have,
                                           int want) const
{
    const int softCap = ThumtooCache::kGalleryLadderEdge;
    // Soft PreferCache stop: only when still below soft max and that edge gave up.
    if (!coversEdge(have, softCap) && st.gaveUpWant >= qMin(want, softCap)) {
        return true;
    }
    if (gallerySoftInflightCount() >= galleryDecodeConcurrency()) {
        return true;
    }
    return false;
}

void ImageView::startGallerySoftClimbJob(const QString &path, int requestEdge,
                                         bool overviewOnly, int want, int have,
                                         GallerySoftState &st)
{
    markGallerySoftInflight(st, requestEdge);
    addPendingWorkspacePath(path);
    emit statusChanged();

    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/gallery: soft request path need=%d have=%d req=%d "
                "overview=%d gaveUp=%d\n",
                want, have, requestEdge, overviewOnly ? 1 : 0, st.gaveUpWant);
    }

    const quint64 gen = m_loadGeneration.load();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen, requestEdge, overviewOnly]() {
        // Soft: PreferCache via loadThumbnail. Overview: schedule only
        // (ladderReady installs). Avoid PreferCache 1024 on the pool thread.
        QImage preview;
        if (!overviewOnly) {
            preview = ImageLoader::loadThumbnail(path, requestEdge);
        } else if (ThumtooCache::isAvailable()) {
            if (!ThumtooCache::isPixelsPending(path, requestEdge)) {
                (void)ThumtooCache::scheduleOverviewPixels(path, requestEdge);
            }
            preview = ImageCache::get(path, requestEdge);
            if (preview.isNull()) {
                preview = ImageCache::get(path);
            }
        }
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, preview, gen, requestEdge]() {
                if (ImageView *const host = guard.data()) {
                    host->applyGallerySoftPoolResult(path, preview, gen, requestEdge);
                }
            },
            Qt::QueuedConnection);
    });
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    if (!isGalleryMode() || path.isEmpty()) {
        return;
    }
    // Size-first still probes in the background, but never blocks decode:
    // provisional layout must still climb to the zoom-appropriate ladder edge.
    if (isProvisionalImageSize(path)) {
        scheduleImageSizeProbe(path);
    }
    GallerySoftState &st = m_gallerySoft[path];
    if (st.failed || st.inflight > 0 || st.fullInflight) {
        return;
    }

    int have = 0;
    int want = 0;
    if (!resolveGallerySoftHaveWant(path, st, &have, &want)) {
        return;
    }

    clearGalleryGaveUpIfClimbable(st, have, want);
    if (gallerySoftScheduleBlocked(st, have, want)) {
        return;
    }

    const int softCap = ThumtooCache::kGalleryLadderEdge;
    const int ovCap = ThumtooCache::kBatchOverviewEdge;
    // Soft / overview plan (pure). Display PreferCache is a separate host path.
    const SoftClimbPlan climb = planSoftClimb(have, want, softCap, ovCap);
    if (climb.kind == SoftClimbPlan::Kind::None) {
        if (want <= ovCap) {
            st.gaveUpWant = qMax(st.gaveUpWant, want);
            return;
        }
        scheduleGalleryDisplayPreferCache(path, st, have, want);
        return;
    }

    startGallerySoftClimbJob(path, climb.edge,
                             climb.kind == SoftClimbPlan::Kind::Overview, want, have,
                             st);
}

void ImageView::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    if (path.isEmpty()) {
        return;
    }
    // Always seed host soft cache so any mode can install even if this
    // completion landed before placeholders existed.
    if (!image.isNull()) {
        ImageCache::put(path, image);
    }
    // PreferCache completion → slideshow pure-phase upgrades from ImageCache.
    if (m_slideshowProgressActive && !image.isNull()) {
        onSlideshowRasterReady(path, image);
    }

    // Image mode: soft→sharp when full ImageLoader::load missed or is still
    // in flight. ladderReady used to return early for non-Gallery, so PDF /
    // page / archive PreferCache deliveries left the view stuck on the soft
    // thumbnail forever.
    if (isImageMode() && !image.isNull() && !m_slideshowProgressActive) {
        upgradeImageModeFromLadder(path, maxEdge, image);
    }

    if (!isGalleryMode()) {
        return;
    }
    applyGalleryLadderReady(path, maxEdge, image);
}

SessionAppearance::PixelKind ImageView::pixelKindForImageModeSample(
    const QString &path, const QImage &image) const
{
    // Classify by *delivered* long edge, never by the request edge.
    const int incoming = ImageCache::longEdge(image);
    if (incoming <= 0) {
        return SessionAppearance::PixelKind::SoftPreview;
    }
    // Native coverage (or PreferCache above soft max when size is provisional).
    if (sampleCoversNativeLogical(path, image)) {
        return SessionAppearance::PixelKind::FullSource;
    }
    // Soft durable ladder — SoftPreview so native / PreferCache can still upgrade.
    if (incoming <= ThumtooCache::kGalleryLadderEdge) {
        return SessionAppearance::PixelKind::SoftPreview;
    }
    // PreferCache display band without native size: FullSource for paint mode;
    // HUD uses sampleCoversNativeLogical, not hasDecodedPixels alone.
    return SessionAppearance::PixelKind::FullSource;
}

void ImageView::upgradeImageModeFromLadder(const QString &path, int maxEdge,
                                           const QImage &image)
{
    // ladderReady Image-mode path: same install policy as completeLoadReplace.
    if (path.isEmpty() || image.isNull() || path != classicPath()) {
        return;
    }
    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        const int req = maxEdge > 0 ? maxEdge : ImageCache::longEdge(image);
        fprintf(stderr,
                "biltoo/image: ladderReady UPGRADE path=%s req=%d got=%dx%d\n",
                qPrintable(QFileInfo(path).fileName()), req, image.width(),
                image.height());
    }
    // Record PreferCache shortfall before install so ensure does not re-queue.
    noteImageModePreferCacheDelivery(path, maxEdge, image);
    (void)tryInstallImageModeSample(path, image);
}

void ImageView::applyGalleryLadderReady(const QString &path, int maxEdge,
                                        const QImage &image)
{
    if (!isGalleryMode() || path.isEmpty()) {
        return;
    }
    const int edge = maxEdge > 0 ? maxEdge : ThumtooCache::kGalleryLadderEdge;
    // Worker already decoded `image` — never PreferCache on the GUI thread.
    const int got = image.isNull() ? 0 : ImageCache::longEdge(image);
    if (!image.isNull()) {
        if (const char *dbg = std::getenv("THUMTOO_DEBUG");
            dbg && dbg[0] && dbg[0] != '0') {
            fprintf(stderr,
                    "biltoo/gallery: ladderReady INSTALL path=%s edge=%d got=%dx%d\n",
                    qPrintable(QFileInfo(path).fileName()), edge, image.width(),
                    image.height());
        }
        onImagePreviewLoaded(path, image, 0, static_cast<int>(LoadAdd));
    } else if (const char *dbg = std::getenv("THUMTOO_DEBUG");
               dbg && dbg[0] && dbg[0] != '0') {
        fprintf(stderr, "biltoo/gallery: ladderReady EMPTY path=%s edge=%d\n",
                qPrintable(QFileInfo(path).fileName()), edge);
    }

    auto it = m_gallerySoft.find(path);
    if (it != m_gallerySoft.end()) {
        it.value().noteLadderDelivery(edge, got, ThumtooCache::kFilmstripLadderEdge);
    }

    // Debounce window rescan — avoid full setInterest on every tile delivery.
    scheduleGalleryDecodeWindowRefresh(150);
    emit statusChanged();
}

void ImageView::onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                                     int role)
{
    if (image.isNull()) {
        return;
    }
    ImageCache::put(path, image);
    // Soft job during slideshow must upgrade phase buffers (m_ssFrom/To), not
    // only ImageCache — otherwise crossfade stays on empty/LQIP until preload.
    if (m_slideshowProgressActive) {
        onSlideshowRasterReady(path, image);
    }

    // Replace navigations: drop superseded previews.
    if (role == LoadReplace) {
        if (generation != m_loadGeneration.load() || path != classicPath()) {
            return;
        }
        if (isImageMode()) {
            // Same install + climb policy as completeLoadReplace / ladderReady.
            (void)tryInstallImageModeSample(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }

    // Gallery / Workspace: fill undecoded occurrences of this path.
    // Layout size stays logical (SIZE.md); installDisplayPixels may only fix
    // 1×1 placeholders via layoutSizeForPath aspect.
    const int incoming = ImageCache::longEdge(image);
    bool gallerySizeChanged = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path || item->hasDecodedPixels()) {
            continue;
        }
        if (!item->shouldUpgradeDisplayTo(incoming)) {
            continue;
        }
        const QSize before = item->imageSize();
        installDisplayPixels(item, image, SessionAppearance::PixelKind::SoftPreview,
                             item->sessionId());
        if (item->imageSize() != before) {
            gallerySizeChanged = true;
        }
        item->update(); // coalesced viewport update below
    }
    if (gallerySizeChanged && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (viewport()) {
        viewport()->update();
    }
}

bool ImageView::takePendingRestoreState(const QString &path, WorkspaceItemState *out)
{
    if (!out || path.isEmpty()) {
        return false;
    }
    for (int i = 0; i < m_pendingRestoreStates.size(); ++i) {
        if (m_pendingRestoreStates.at(i).path == path) {
            *out = m_pendingRestoreStates.takeAt(i);
            return true;
        }
    }
    return false;
}

void ImageView::completeLoadRestore(const QString &path, const QImage &image)
{
    WorkspaceItemState state;
    if (!takePendingRestoreState(path, &state) || image.isNull()) {
        return;
    }
    // Do not apply path-keyed crop — restore uses this slot's own state
    // (Workspace duplicates must not inherit another instance's crop).
    ImageItem *item = createItemFromImage(path, image, /*applyStoredSessionCrop=*/false);
    if (!item) {
        return;
    }
    // Prefer live session-image appearance over the leave-mode snapshot when
    // Image-mode edits updated m_appearance while Workspace was stashed.
    WorkspaceItemState app = state;
    if (state.sessionId != kInvalidSessionImageId) {
        item->setSessionId(state.sessionId);
        if (const WorkspaceItemState *it = m_appearance.get(state.sessionId)) {
            app = *it;
            // Keep placement from the snapshot.
            app.pos = state.pos;
            app.scale = state.scale;
            app.scaleY = state.scaleY;
            app.rotation = state.rotation;
            app.opacity = state.opacity;
            app.z = state.z;
        }
    }
    if (state.sessionIndex >= 0) {
        item->setSessionIndex(state.sessionIndex);
    }
    SessionAppearance::applyContentToItem(item, app);
    applyState(item, app);
    if (m_layoutMode != LayoutMode::FreeForm
        && !(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
        applyLayout(GalleryPackReason::SessionMutate);
    }
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::finishLoadAddStatus(bool refreshGalleryWindow)
{
    emit statusChanged();
    if (refreshGalleryWindow && isGalleryMode()) {
        scheduleGalleryDecodeWindowRefresh(48);
    }
}

bool ImageView::acceptPendingLoadAdd(const QString &path, quint64 generation)
{
    // Mode leave / empty Workspace bumps generation and clears pending paths.
    // Reject superseded gallery window decodes so they cannot spawn tiles on
    // Workspace after the user switched modes mid-decode.
    if (generation != m_loadGeneration.load()) {
        finishLoadAddStatus(/*refreshGalleryWindow=*/false);
        return false;
    }
    if (!m_pendingWorkspacePaths.contains(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return false;
    }
    takePendingWorkspacePath(path);
    return true;
}

void ImageView::handleLoadAddDecodeFailure(const QString &path)
{
    // //pdfimage: / //page: with thumtoo: empty sync load is expected while the
    // ladder builds — await ladderReady instead of permanent failure.
    if (ThumtooCache::isAvailable()
        && (PagePath::isPdfImageRef(path) || PagePath::isPageRef(path))) {
        ThumtooCache::scheduleProbe(path);
        // Soft state machine will request placeholder / higher steps.
        m_lastLoadError.clear();
        finishLoadAddStatus(/*refreshGalleryWindow=*/true);
        return;
    }
    qWarning("ImageView: decode failed for %s", qPrintable(path));
    if (isGalleryMode()) {
        GallerySoftState &st = m_gallerySoft[path];
        st.failed = true;
        st.inflight = 0;
    }
    m_lastLoadError = path;
    // Surface the error on any live placeholder for this path.
    for (ImageItem *item : m_items) {
        if (item && item->path() == path && !item->hasDecodedPixels()) {
            item->setToolTip(tr("Failed to load:\n%1").arg(path));
        }
    }
    finishLoadAddStatus(/*refreshGalleryWindow=*/true);
}

void ImageView::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    for (ImageItem *cand : m_gallery.stashedItems()) {
        if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
            installDisplayPixels(cand, image,
                                 SessionAppearance::PixelKind::FullSource,
                                 cand->sessionId());
        }
    }
}

void ImageView::reassertPendingBindPlacement(const QString &path)
{
    // Drop placeholders created in placeOrMoveImageAt already sit at scenePos;
    // re-assert hasScenePos binds so nothing later drifts them, and so a bind
    // still pairs with the pre-created tile.
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            const PendingSessionBind &b = m_pendingSessionBinds.at(bi);
            if (b.path != path) {
                continue;
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() != kInvalidSessionImageId
                && b.id != item->sessionId()) {
                continue;
            }
            if (b.hasScenePos) {
                item->setGalleryCellSize({});
                item->setPos(b.scenePos);
                item->setItemScale(1.0);
                item->setItemRotation(0.0);
                item->setItemShear(0.0);
                item->setItemOpacity(1.0);
                if (isWorkspaceMode()) {
                    item->setInteractive(true);
                    item->setScaleHandlesEnabled(true);
                }
            }
            if (b.id != kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
                item->setSessionId(b.id);
            }
            if (b.index >= 0 && item->sessionIndex() < 0) {
                item->setSessionIndex(b.index);
            }
            break;
        }
    }
}

void ImageView::claimUnboundItemsForPendingBinds(const QString &path, const QImage &image)
{
    // Claim existing *unbound* tiles of this path for pending session binds
    // (e.g. empty-Workspace LoadReplace seeded the first path before LoadAdd).
    // Without this, have==wanted and the bind is never applied — placement,
    // content flips, and colour grade stay at defaults on that tile.
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        if (existing->sessionId() != kInvalidSessionImageId) {
            continue;
        }
        PendingSessionBind bound;
        if (!takePendingSessionBind(path, &bound)) {
            break;
        }
        if (bound.id != kInvalidSessionImageId) {
            existing->setSessionId(bound.id);
        }
        if (bound.index >= 0 && bound.id != kInvalidSessionImageId) {
            existing->setSessionIndex(bound.index);
        }
        // Raw full decode → single appearance gate (seed tiles may already
        // have decoded defaults without content ops).
        installDisplayPixels(existing, image,
                             SessionAppearance::PixelKind::FullSource,
                             bound.id != kInvalidSessionImageId
                                 ? bound.id
                                 : existing->sessionId());
        if (bound.id != kInvalidSessionImageId && m_appearance.get(bound.id)) {
            applyState(existing, *m_appearance.get(bound.id));
        }
        // Explicit drop position wins over restored gallery/workspace pose.
        applyPendingBindScenePos(existing, bound);
        if (bound.id != kInvalidSessionImageId) {
            // Decode must not rewrite filmstrip (sessionAppearanceChanged).
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                existing->setSelected(true);
            }
        }
    }
}

int ImageView::fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                              bool *sizeChangedOut)
{
    bool sizeChanged = false;
    int have = 0;
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        if (!existing->hasDecodedPixels()) {
            if (installFullPreservingWorkspaceFootprint(existing, image)) {
                sizeChanged = true;
            }
        }
    }
    if (sizeChangedOut) {
        *sizeChangedOut = sizeChanged;
    }
    return have;
}

void ImageView::createMissingLoadAddItems(const QString &path, const QImage &image,
                                          int have, int wanted)
{
    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {
        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        PendingSessionBind bound;
        const bool haveBound = takePendingSessionBindForNewItem(path, item, &bound);
        applyStoredAppearance(item);
        // Decode/membership must not rewrite filmstrip; user edits emit overrides.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            // Paste: select tiles as they finish decoding.
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                item->setSelected(true);
            }
        }
        placeNewLoadAddItem(item, path, image, haveBound, bound);
    }
}

void ImageView::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    if (m_layoutMode != LayoutMode::FreeForm) {
        if (!m_pathOrder.isEmpty()) {
            reorderItemsByPaths(m_pathOrder);
        }
        if (!(isGalleryMode() && m_galleryRelayoutSuppressCount > 0)) {
            if (sizeChanged) {
                applyLayout(GalleryPackReason::ContentChange);
            } else {
                applyLayout(GalleryPackReason::SessionMutate);
            }
        }
    } else {
        updateWorkspaceSceneRect();
    }
}

void ImageView::completeLoadAdd(const QString &path, const QImage &image, quint64 generation)
{
    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window.
    // Duplicate paths are separate session images: fill every undecoded live
    // occurrence, then create until live count matches pathOrder occurrences.
    gallerySoftResetPath(path);

    // Remember size even when the pending membership was cancelled — a successful
    // decode still updates the session size cache for later layout.
    if (generation == m_loadGeneration.load() && !image.isNull()) {
        rememberSizeFromDecode(path, image);
    }
    if (!acceptPendingLoadAdd(path, generation)) {
        return;
    }
    if (image.isNull()) {
        handleLoadAddDecodeFailure(path);
        return;
    }

    if (isImageMode()) {
        // Fill stashed Gallery placeholders while user is in Image mode.
        fillStashedItemsForPath(path, image);
        emit statusChanged();
        return;
    }

    reassertPendingBindPlacement(path);

    const int pathOrderCount = pathOrderOccurrences(path);

    // Pending binds whose SessionImageId is already on a live tile are satisfied.
    purgeSatisfiedPendingBinds(path);

    claimUnboundItemsForPendingBinds(path, image);

    const int pendingBinds = countPendingSessionBinds(path);

    bool sizeChanged = false;
    int have = fillLiveItemsWithDecodedPixels(path, image, &sizeChanged);
    fillStashedItemsForPath(path, image);

    // Session pathOrder is the multiplicity source of truth. Do not create more
    // tiles than session rows for this path (pending binds only fill gaps).
    int wanted = pathOrderCount;
    if (wanted <= 0) {
        // Not in session pathOrder (ad-hoc workspace place): one tile per bind.
        wanted = qMax(have, pendingBinds > 0 ? pendingBinds : 1);
    }

    createMissingLoadAddItems(path, image, have, wanted);
    applyLoadAddLayoutAfterMembership(sizeChanged);

    emit statusChanged();
    emit workspacePathsChanged();
    if (isGalleryMode()) {
        scheduleGalleryDecodeWindowRefresh(48);
    }
}

void ImageView::applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path)
{
    if (!item || path.isEmpty()) {
        return;
    }
    // Content 90°/flip are already in pixels (createItemFromImage bakes).
    // Legacy unbaked flips only if content flags not used yet.
    const auto it = m_itemStates.constFind(path);
    if (it == m_itemStates.cend()) {
        return;
    }
    if (!it->contentHFlip && !it->contentVFlip) {
        item->setItemHFlip(it->hFlip);
        item->setItemVFlip(it->vFlip);
    }
}

void ImageView::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    if (!item) {
        return;
    }
    // Slideshow framing: when dwell motion is on, the camera sets the
    // transform (including handoff from a live transition). Applying zoom
    // framing first would centre the image then jump to motion t0.
    if (m_slideshowProgressActive && m_slideshowMotion == SlideshowMotion::Off) {
        applySlideshowZoomFraming(item);
    } else if (!m_slideshowProgressActive) {
        fitItem(item, currentFitAspectMode());
    }
    m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    maybeStartSlideshowMotion();
    if (m_slideshowProgressActive && m_slideshowMotion != SlideshowMotion::Off
        && !m_slideshowMotionActive) {
        applySlideshowZoomFraming(item);
    }
    if (m_slideshowProgressActive) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        setSlideshowPhase(path, QString(), -1.0);
    }
}

void ImageView::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    // Suppress paints between removing the old item and fitting the new one
    // so we never present a native-scale (or empty) intermediate frame.
    setUpdatesEnabled(false);
    // Keep stashed Workspace/Gallery tiles — only replace the Image-mode item.
    clearLiveCanvas();
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        setUpdatesEnabled(true);
        m_lastLoadError = path;
        emit statusChanged();
        return;
    }
    // Filmstrip overrides are not driven by decode (selection/nav).
    // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
    // Arbitrary Workspace rotation stays on the free-form item only.
    // Crop was applied in createItemFromImage from m_itemStates.
    bindImageModeSessionCursor(item);
    resetImageModeItemPlacement(item);
    applyLegacyPathFlipsIfNeeded(item, path);
    prepareImageModeCanvas();
    frameImageModeReplaceItem(item, path);
    setUpdatesEnabled(true);
    if (viewport()) {
        viewport()->update();
    }
    emit statusChanged();
}

void ImageView::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's LoadAdd.
    if (!m_items.isEmpty()
        || !m_pendingSessionBinds.isEmpty()
        || m_pendingWorkspacePaths.contains(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    item->setSelected(true);
    m_fitMode = true;
    fitItem(item, currentFitAspectMode());
    emit statusChanged();
}


ImageItem *ImageView::imageModeItemForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    ImageItem *cur = targetItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    cur = primaryItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    return nullptr;
}

void ImageView::scheduleImageModePreferCacheClimb(const QString &path, int wantEdge)
{
    if (!ThumtooCache::isAvailable() || path.isEmpty()) {
        return;
    }
    if (ImageItem *it = imageModeItemForPath(path)) {
        if (it->displayPixelLongEdge() <= 0) {
            biltooLoadDbg("WARN preferCache before soft paint path=%s want=%d",
                          qPrintable(QFileInfo(path).fileName()), wantEdge);
        }
    }
    ImageModeClimbState &st = m_imageModeClimb[path];
    const int edge = wantEdge > 0
                         ? qMin(wantEdge, ThumtooCache::kImageLadderEdge)
                         : ThumtooCache::kImageLadderEdge;
    if (!st.shouldScheduleDisplay(edge)) {
        return;
    }
    st.markDisplayScheduled(edge);
    biltooLoadDbg("preferCacheClimb path=%s edge=%d have=%d",
                  qPrintable(QFileInfo(path).fileName()), edge, st.have);
    ThumtooCache::scheduleProbe(path);
    // Soft band once; settled soft skips re-queue (no forgetPixelsSettled).
    (void)ThumtooCache::schedulePixels(path, ThumtooCache::kGalleryLadderEdge);
    (void)ThumtooCache::scheduleDisplayPixels(path, edge);
}

void ImageView::installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                             const QImage &image,
                                             SessionAppearance::PixelKind kind)
{
    if (!item || image.isNull()) {
        return;
    }
    // Fast path: worker-baked sample — assign + repaint only.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        item->setPreviewImage(image);
    } else {
        item->setSourceImageReady(image);
    }
    m_lastLoadError.clear();
    rememberSizeFromDecode(path, image);
    if (viewport()) {
        viewport()->update();
    }
}

bool ImageView::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    // True when the sample is good enough to stop PreferCache / soft climb.
    // Soft band is never final; PreferCache above soft max is only final when
    // native size is unknown (provisional). Known native uses ~90% coverage.
    const int incoming = ImageCache::longEdge(image);
    if (incoming <= 0) {
        return false;
    }
    const QSize logical = logicalSizeForPath(path);
    if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
        return incoming > ThumtooCache::kGalleryLadderEdge;
    }
    const int native = qMax(logical.width(), logical.height());
    return coversEdge(incoming, native);
}

void ImageView::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    // PreferCache shortfall (tile_synth plateau): stop re-requesting the same edge.
    if (path.isEmpty()) {
        return;
    }
    ImageModeClimbState &st = m_imageModeClimb[path];
    const bool wasGaveUp = st.preferGaveUp;
    st.noteDelivery(requestEdge, ImageCache::longEdge(sample));
    if (!wasGaveUp && st.preferGaveUp) {
        if (const char *dbg = std::getenv("THUMTOO_DEBUG");
            dbg && dbg[0] && dbg[0] != '0') {
            fprintf(stderr,
                    "biltoo/image: PreferCache gave up path=%s req=%d got=%d\n",
                    qPrintable(QFileInfo(path).fileName()), requestEdge,
                    ImageCache::longEdge(sample));
        }
    }
}

void ImageView::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    // Soft is already on screen (or pending). Request higher tiers in the
    // background only — never block the GUI, never cancel soft display.
    // High-res PreferCache (up to kImageLadderEdge / on-screen need) always
    // runs; native extract is a last resort after PreferCache plateaus.
    if (path.isEmpty()) {
        return;
    }
    if (!sample.isNull() && sampleCoversNativeLogical(path, sample)) {
        m_imageModeClimb.remove(path);
        return;
    }

    const int need = imageModeOnScreenNeedEdge();
    const int have = sample.isNull() ? 0 : ImageCache::longEdge(sample);
    ImageModeClimbState &st = m_imageModeClimb[path];
    if (!sample.isNull()) {
        st.have = qMax(st.have, have);
    }

    // Background target: at least overview, at most ladder, prefer on-screen need.
    int climbTo = ThumtooCache::kBatchOverviewEdge;
    if (need > 0) {
        climbTo = qMax(climbTo, need);
    }
    climbTo = qMin(climbTo, ThumtooCache::kImageLadderEdge);
    if (climbTo < ThumtooCache::kGalleryLadderEdge) {
        climbTo = ThumtooCache::kGalleryLadderEdge;
    }

    if (!st.preferGaveUp) {
        scheduleImageModePreferCacheClimb(path, climbTo);
    } else if (need > 0 && !coversEdge(have, need)) {
        // PreferCache plateaued below viewport need — quiet native as fallback.
        scheduleImageModeNativeFullQuiet(path);
    }
}

bool ImageView::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    if (!isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    const SessionAppearance::PixelKind kind = pixelKindForImageModeSample(path, image);
    if (ImageItem *cur = imageModeItemForPath(path)) {
        if (canAcceptDisplaySample(cur, image, kind)) {
            installImageModeSampleInPlace(cur, path, image, kind);
            biltooLoadDbg("tryInstall OK path=%s kind=%d edge=%d",
                          qPrintable(QFileInfo(path).fileName()),
                          int(kind), ImageCache::longEdge(image));
        } else {
            biltooLoadDbg("tryInstall REJECT path=%s kind=%d edge=%d have=%d decoded=%d",
                          qPrintable(QFileInfo(path).fileName()),
                          int(kind), ImageCache::longEdge(image),
                          cur->displayPixelLongEdge(),
                          cur->hasDecodedPixels() ? 1 : 0);
        }
        // Even when the sample is not an upgrade (already showing equal soft),
        // keep climbing until native coverage — otherwise soft latches forever.
        if (kind == SessionAppearance::PixelKind::SoftPreview
            || !sampleCoversNativeLogical(path, image)) {
            ensureImageModeQualityClimb(path, image);
        }
        return true;
    }
    // First install for this path: SoftPreview uses pending-tile path so
    // FullSource-only createItemFromImage is not forced on a soft sample.
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        installImageModePendingTile(path, image);
        ensureImageModeQualityClimb(path, image);
        emit statusChanged();
    } else {
        installImageModeReplaceItem(path, image);
        if (!sampleCoversNativeLogical(path, image)) {
            ensureImageModeQualityClimb(path, image);
        }
    }
    return true;
}

int ImageView::imageModeOnScreenNeedEdge() const
{
    if (!isImageMode()) {
        return 0;
    }
    const ImageItem *item = targetItem();
    if (!item) {
        item = primaryItem();
    }
    return itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
}

void ImageView::finishImageModeNativeFullQuiet(const QString &path, const QImage &image,
                                               quint64 generation)
{
    // GUI-thread completion for scheduleImageModeNativeFullQuiet.
    m_imageModeNativeClimbPaths.remove(path);
    if (generation != m_loadGeneration.load() || image.isNull()) {
        return;
    }
    (void)tryInstallImageModeSample(path, image);
}

void ImageView::scheduleImageModeNativeFullQuiet(const QString &path)
{
    // Native full without LoadReplace generation bump / pending tile (zoom climb).
    if (path.isEmpty() || m_imageModeNativeClimbPaths.contains(path)) {
        return;
    }
    m_imageModeNativeClimbPaths.insert(path);
    const QPointer<ImageView> guard(this);
    const quint64 gen = m_loadGeneration.load();
    const QString pathCopy = path;
    QThreadPool::globalInstance()->start(
        [guard, pathCopy, gen]() {
            if (!guard || !guard->matchesLoadGeneration(gen)) {
                // Drop the inflight mark on the GUI thread if the view still lives.
                if (ImageView *view = guard.data()) {
                    QTimer::singleShot(0, view, [view, pathCopy]() {
                        view->m_imageModeNativeClimbPaths.remove(pathCopy);
                    });
                }
                return;
            }
            QImage image = ImageLoader::load(pathCopy);
            if (!image.isNull()) {
                image = prepareImageModeDisplaySample(
                    pathCopy, image, SessionAppearance::PixelKind::FullSource);
            }
            ImageView *view = guard.data();
            if (!view) {
                return;
            }
            // Named finish: clear path + install on GUI thread (no nested QPointer).
            QTimer::singleShot(0, view, [view, pathCopy, image, gen]() {
                view->finishImageModeNativeFullQuiet(pathCopy, image, gen);
            });
        },
        -1);
}

void ImageView::maybeClimbImageModePixelsForView()
{
    // Zoom / resize: PreferCache climbs when on-screen need exceeds painted.
    // Do not start Display@ladder while soft is still missing — that races the
    // soft 512 job and is what THUMTOO_DEBUG showed as need=2048 decoded=0.
    if (!isImageMode() || m_slideshowProgressActive) {
        return;
    }
    ImageItem *item = imageModeItemForPath(classicPath());
    if (!item) {
        item = targetItem();
    }
    if (!item || item->path().isEmpty()) {
        return;
    }
    const QString path = item->path();
    const int need = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
    const int have = item->displayPixelLongEdge();
    if (have <= 0) {
        // Soft / LQIP not installed yet — ensureImageModeQualityClimb runs
        // after soft lands; PreferCache waits for that.
        return;
    }
    if (need <= 0 || coversEdge(have, need)) {
        return;
    }

    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/image: view climb path=%s have=%d need=%d decoded=%d\n",
                qPrintable(QFileInfo(path).fileName()), have, need,
                item->hasDecodedPixels() ? 1 : 0);
    }

    // Zoom-in only: PreferCache when on-screen need exceeds last request.
    ImageModeClimbState &st = m_imageModeClimb[path];
    if (st.shouldScheduleDisplay(need)) {
        scheduleImageModePreferCacheClimb(path, need);
    }
    scheduleImageModeNativeFullQuiet(path);
}


void ImageView::completeLoadReplace(const QString &path, const QImage &image, quint64 generation)
{
    if (generation != m_loadGeneration.load()) {
        return; // superseded by a newer navigation / open
    }
    // Stale navigation: only the current classic path may install.
    // Empty multi-item canvas can still seed from classicPath.
    if (path != classicPath()) {
        return;
    }
    if (image.isNull()) {
        if (ThumtooCache::isAvailable()) {
            // Full native miss: PreferCache display ladder so onLadderReady can
            // upgrade Image mode (soft→HQ).
            scheduleImageModePreferCacheClimb(path, ThumtooCache::kBatchOverviewEdge);
            m_lastLoadError.clear();
        } else {
            m_lastLoadError = path;
        }
        emit statusChanged();
        return;
    }
    if (isImageMode()) {
        (void)tryInstallImageModeSample(path, image);
        return;
    }
    seedEmptyWorkspaceFromReplace(path, image);
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Host path→raster map: keep display-ladder samples (≤ kDisplayMaxEdge),
    // not a hard 512 preview. Slideshow must reuse Image-mode sharpness.
    if (!image.isNull() && !path.isEmpty()) {
        ImageCache::put(path, image);
        // Quality/soft climb during slideshow → phase buffer + atlas upgrade.
        if (m_slideshowProgressActive) {
            onSlideshowRasterReady(path, image);
        }
    }
    switch (static_cast<LoadRole>(role)) {
    case LoadReplace:
        completeLoadReplace(path, image, generation);
        break;
    case LoadRestore:
        completeLoadRestore(path, image);
        break;
    case LoadAdd:
        completeLoadAdd(path, image, generation);
        break;
    }
}


bool ImageView::loadImage(const QString &path)
{
    setClassicPath(path);
    clearTextSelection();
    m_linkHoverTip.clear();
    if (m_showTextRegions || !m_textSearchQuery.isEmpty()) {
        refreshTextLayer();
    }
    m_lastLoadError.clear();

    if (isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_items.isEmpty()) {
            scheduleImageLoad(path, LoadReplace);
        }
        emit statusChanged();
        return true;
    }

    // Classic mode: soft from cache immediately; PreferCache climbs in background.
    // Do not emit statusChanged — setCurrentIndex chrome already updateStatus.
    scheduleImageLoad(path, LoadReplace);
    return true;
}
