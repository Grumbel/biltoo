// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "displayquality.h"

#include "archivepath.h"
#include "imagecache.h"
#include "imageitem.h"
#include "imageloader.h"
#include "pagepath.h"
#include "sessionappearance.h"
#include "contentxform.h"
#include "thumtoocache.h"
#if __has_include("thumtoo/client.hpp")
#include "thumtoo/client.hpp"
#endif
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
                                     SessionAppearance::PixelKind kind,
                                     const WorkspaceItemState *sessionApp = nullptr)
{
    ASSERT_NOT_GUI_THREAD();
    if (raw.isNull()) {
        return {};
    }
    WorkspaceItemState app;
    // Session crop/flips are keyed by SessionImageId (m_appearance), not path.
    // Gallery→Image must pass a snapshot; durable path store alone misses
    // in-session crops that were never written to thumtoo.
    if (sessionApp
        && (SessionAppearance::hasContentAppearance(*sessionApp)
            || !sessionApp->colorAdjust.isIdentity())) {
        app = *sessionApp;
    } else {
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
                         quint64 gen, int roleInt, int softEdge,
                         const WorkspaceItemState &sessionApp)
{
    biltooLoadDbg("softJob START path=%s edge=%d gen=%llu",
                  qPrintable(QFileInfo(path).fileName()), softEdge,
                  static_cast<unsigned long long>(gen));
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, softEdge, sessionApp]() {
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
                    path, preview, SessionAppearance::PixelKind::SoftPreview,
                    &sessionApp);
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
                            quint64 gen, int roleInt, int qualityEdge,
                            const WorkspaceItemState &sessionApp)
{
    QThreadPool::globalInstance()->start(
        [guard, path, roleInt, gen, qualityEdge, sessionApp]() {
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
                // PreferCache/Full only via PathRaster on the GUI (contract §1).
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
                return;
            }
            // Soft stand-in still upgrades the view; climb via PathRaster on GUI.
            if (!ImageCache::adequate(image, qualityEdge) && ThumtooCache::isAvailable()) {
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard, path, qualityEdge]() {
                        if (guard) {
                            guard->requestEscalateClimb(path, qualityEdge);
                        }
                    },
                    Qt::QueuedConnection);
            }
            if (!image.isNull()) {
                image = prepareImageModeDisplaySample(
                    path, image, SessionAppearance::PixelKind::FullSource,
                    &sessionApp);
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
    // Prefer ContentXform::layoutSize when appearance is known; else path layout.
    WorkspaceItemState app;
    if (applyStoredSessionCrop && isImageMode()) {
        const bool haveId = m_currentSessionId != kInvalidSessionImageId;
        const bool havePath = m_itemStates.contains(path);
        if ((haveId || havePath)
            && !(haveId && !m_appearance.get(m_currentSessionId))) {
            app = appearanceForNewImageModeItem(path);
        }
    }
    QSize native = layoutSizeForPath(path, image);
    if (!isPositiveSize(native) || native.width() <= 1 || native.height() <= 1) {
        native = image.size();
    }
    QSize intrinsic = ContentXform::layoutSize(native, app);
    if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
        intrinsic = QSize(1, 1);
    }
    if (app.hasCrop && !app.cropRect.isEmpty()
        && image.width() > 1 && image.height() > 1) {
        // Worker-baked crop: display sample is the identity box.
        intrinsic = image.size();
    }
    auto *item = new ImageItem(path, intrinsic);
    if (isImageMode()) {
        item->setSourceImageReady(image);
    } else {
        item->setSourceImage(image);
    }
    applyItemModeFlags(item);
    // Session crop survives navigation. Image mode LoadReplace jobs bake
    // appearance on the worker — only sync chrome flags here.
    if (applyStoredSessionCrop && isImageMode()
        && (app.hasCrop || app.contentHFlip || app.contentVFlip
            || app.contentQuarterTurns != 0 || !app.colorAdjust.isIdentity())) {
        item->setContentHFlip(app.contentHFlip);
        item->setContentVFlip(app.contentVFlip);
        item->setSessionCrop(app.hasCrop, app.cropRect);
        item->setColorAdjustmentsRecord(app.colorAdjust);
    }
    // Fingerprint: sample is already display (worker-baked or identity).
    item->setAppliedContentXform(ContentXform::Value::fromState(app));
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

WorkspaceItemState ImageView::wantAppearanceForItem(const ImageItem *item,
                                                      SessionImageId sid) const
{
    WorkspaceItemState appearance;
    if (!item) {
        return appearance;
    }
    SessionImageId id = sid;
    if (id == kInvalidSessionImageId) {
        id = item->sessionId();
    }
    if (id == kInvalidSessionImageId && isImageMode()) {
        id = m_currentSessionId;
    }
    if (id != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = m_appearance.get(id)) {
            appearance = *app;
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            appearance = *it;
        }
    }
    // Live flags on the item win when the store is still empty for flips/crop.
    if (item->contentHFlip()) {
        appearance.contentHFlip = true;
    }
    if (item->contentVFlip()) {
        appearance.contentVFlip = true;
    }
    if (item->sessionHasCrop() && appearance.cropRect.isEmpty()) {
        appearance.hasCrop = true;
        appearance.cropRect = item->sessionCropRect();
    }
    return appearance;
}

bool ImageView::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    // Soft must not demote full. Otherwise accept when ContentXform says
    // rematerialize (xform change or strict edge upgrade) or the tile is blank.
    if (!item || pixels.isNull()) {
        return false;
    }
    const int incoming = ImageCache::longEdge(pixels);
    if (incoming <= 0) {
        return false;
    }
    if (kind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        return false;
    }
    if (!item->hasDisplayPixels()) {
        return true;
    }
    const WorkspaceItemState wantState = wantAppearanceForItem(item, item->sessionId());
    const ContentXform::Value want = ContentXform::Value::fromState(wantState);
    const ContentXform::Value applied = item->hasAppliedContentXform()
        ? item->appliedContentXform()
        : ContentXform::Value{};
    const int shown = item->displayPixelLongEdge();
    // No applied fingerprint yet: fall back to edge-only (pre-tag tiles).
    if (!item->hasAppliedContentXform()) {
        return item->shouldUpgradeDisplayTo(incoming)
            || ContentXform::needsRematerialize(applied, want, shown, incoming);
    }
    return ContentXform::needsRematerialize(applied, want, shown, incoming);
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

    // Absolute want xform (session store / path map / live flags).
    const WorkspaceItemState appearance = wantAppearanceForItem(item, sid);

    // raw → optional gallery soft clamp → materializeDisplay → attach.
    QImage pixelsForDisplay = pixels;
    if (isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        pixelsForDisplay = clampSoftForGalleryCell(
            pixels,
            galleryDisplayEdgeForItem(item, /*allowHighRes=*/true),
            ThumtooCache::kFilmstripLadderEdge);
    }
    // Image mode LoadReplace jobs bake appearance on the worker
    // (prepareImageModeDisplaySample). Gallery soft samples are ≤512 so a GUI
    // materialize is cheap and allowed. Multi-MP materializeDisplay asserts off
    // the GUI thread — never call it here for large samples (would abort on
    // ←/→ and crop/appearance installs).
    QImage display = pixelsForDisplay;
    const bool wantBake =
        SessionAppearance::hasContentAppearance(appearance)
        || !appearance.colorAdjust.isIdentity();
    if (wantBake) {
        int edge = qMax(pixelsForDisplay.width(), pixelsForDisplay.height());
        // Soft stand-in ≤kGuiMaterializeMaxEdge so materialize is GUI-safe.
        if (edge > ContentXform::kGuiMaterializeMaxEdge
            && kind == SessionAppearance::PixelKind::SoftPreview) {
            pixelsForDisplay = ImageCache::clampToMaxEdge(
                pixelsForDisplay, ContentXform::kGuiMaterializeMaxEdge);
            edge = qMax(pixelsForDisplay.width(), pixelsForDisplay.height());
        }
        if (edge <= ContentXform::kGuiMaterializeMaxEdge) {
            display = SessionAppearance::materializeDisplay(
                pixelsForDisplay, appearance, kind);
        } else if (item->hasDisplayPixels()
                   && SessionAppearance::hasContentAppearance(appearance)) {
            // Multi-MP: cannot materialize on GUI. Keep current display pixels;
            // tag want and schedule pure rematerialize from host (same as rotate).
            item->setAppliedContentXform(
                ContentXform::Value::fromState(appearance));
            scheduleAsyncHostRematerialize(path, sid, appearance);
            return;
        }
        // Cold open: attach raw until soft ≤kGui or worker-baked FullSource.
    }
    const QSize sizeBeforeAttach = item->imageSize();
    attachDisplaySample(item, display, appearance, kind);
    // Soft→layout may change aspect; keep Image view scale continuous.
    if (isImageMode() && item == targetItem()
        && sizeBeforeAttach != item->imageSize()
        && sizeBeforeAttach.width() > 1 && sizeBeforeAttach.height() > 1) {
        preserveImageViewOnLogicalSizeChange(item, sizeBeforeAttach, item->imageSize());
    } else if (isImageMode() && m_scene && m_items.size() == 1) {
        m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    }

    // Do NOT emit sessionAppearanceChanged from decode/install (filmstrip is
    // selection-coupled). Soft ladder upgrades must not rewrite the strip.

    // Gallery reflow only when layout geometry actually changed.
    if (isGalleryMode() && item->imageSize() != layoutBefore) {
        requestDebouncedGalleryPack(GalleryPackReason::ContentChange);
    }
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    // Size-first Gallery open: no scene tiles until definitive sizes land.
    if (m_gallerySizeResolveActive || m_galleryDeferPopulate) {
        return nullptr;
    }
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

    // Cold path: no soft/LQIP yet. Within the *same* path, keep the prior frame
    // until soft arrives (avoids a flash on PreferCache gaps). Different path
    // (session switch / ←→): never keep the previous file's pixels — that is
    // the race where an old session image remains on screen as the new path.
    if (pixels.isNull()) {
        if (m_items.size() == 1) {
            ImageItem *item = m_items.first();
            const bool pathChanged = item->path() != path;
            item->setPath(path);
            bindImageModeSessionCursor(item);
            if (pathChanged) {
                if (item->hasDecodedPixels()) {
                    item->clearDecodedPixels();
                }
                item->setPreviewImage(QImage());
                const QSize sz = layoutSizeForPath(path, QImage());
                if (isPositiveSize(sz)) {
                    item->setIntrinsicSize(sz);
                    // Avoid leaving prior image's sceneRect (free/asymmetric pan)
                    // until soft/full framing runs.
                    syncImageModeSceneRect(item);
                }
                if (viewport()) {
                    viewport()->update();
                }
                biltooLoadDbg("pendingTile DEFER blank path=%s (cleared prior)",
                              qPrintable(QFileInfo(path).fileName()));
            } else {
                biltooLoadDbg("pendingTile DEFER empty soft path=%s keep prior frame",
                              qPrintable(QFileInfo(path).fileName()));
            }
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

    // Layout size = native when known; else preview aspect.
    const QSize sz = layoutSizeForPath(path, pixels);

    // Fast path: reuse the single Image-mode item.
    // Do NOT setUpdatesEnabled(false) — that defers soft paint until after the
    // whole key handler (chrome + climb schedule); user never sees the soft.
    ImageItem *item = nullptr;
    if (m_items.size() == 1) {
        item = m_items.first();
    }
    if (item) {
        const QSize sizeBefore = item->imageSize();
        // Capture only when navigating to a different file — same-path soft→HQ
        // upgrades must not replace a user pan with a stale pre-frame anchor.
        if (item->path() != path) {
            captureStickyPanAnchor(item);
        }
        item->setPath(path);
        bindImageModeSessionCursor(item);
        if (!path.isEmpty()) {
            ImageCache::put(path, pixels);
        }
        // Clear prior FullSource so soft can attach (canAccept rejects soft over full).
        if (item->hasDecodedPixels()) {
            item->clearDecodedPixels();
        }
        // Content pipeline: materialize + applied fingerprint (not bare setPreview).
        installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                             item->sessionId());

        // Intrinsic already set by attachDisplaySample (file-native × want).
        // Only re-frame when we have a definitive file size to orient.
        const QSize known = logicalSizeForPath(path);
        const WorkspaceItemState want = wantAppearanceForItem(item, item->sessionId());
        QSize targetSize = item->imageSize();
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !isProvisionalImageSize(path)) {
            targetSize = ContentXform::layoutSize(known, want);
        } else if (!(isPositiveSize(targetSize) && targetSize.width() > 1)) {
            targetSize = (sz.width() > 1 && sz.height() > 1) ? sz : sizeBefore;
        }
        int didFit = 0;
        if (isPositiveSize(targetSize) && targetSize.width() > 1) {
            item->setIntrinsicSize(targetSize);
            const bool needFit =
                sizeBefore.width() <= 1
                || qAbs(double(sizeBefore.width()) / qMax(1, sizeBefore.height())
                        - double(targetSize.width()) / qMax(1, targetSize.height()))
                       > 0.02;
            if (needFit || m_stickyZoomEnabled || m_havePreservedViewScale) {
                // Aspect change, sticky mode, or free-zoom preserve across files.
                resetImageModeItemPlacement(item);
                applyImageModeFraming(item);
                didFit = 1;
            } else if (sizeBefore != targetSize) {
                preserveImageViewOnLogicalSizeChange(item, sizeBefore, targetSize);
            }
        }
        // Single press: sync repaint so soft is visible before PreferCache.
        // Key-repeat (nav hot): async update only — sync repaint every auto-repeat
        // event was the cumulative GUI freeze under held ←/→.
        if (viewport()) {
            if (m_slideshowNavHot) {
                viewport()->update();
            } else {
                viewport()->repaint();
            }
        }
        biltooLoadDbg("pendingTile INSTALLED path=%s soft=%dx%d fit=%d painted=%s",
                      qPrintable(QFileInfo(path).fileName()),
                      pixels.width(), pixels.height(), didFit,
                      m_slideshowNavHot ? "async" : "sync");
        return;
    }

    // No reusable item — still try to capture from whatever was on the canvas.
    if (!m_items.isEmpty()) {
        captureStickyPanAnchor(m_items.first());
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
    applyImageModeFraming(item);
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
        m_imageModeNativeDecodePaths.clear();
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

    // Slideshow owns the viewport via pure-phase buffers — never soft-install
    // or PreferCache-climb the underlay ImageItem while the show is running.
    if (role == LoadReplace && isImageMode() && m_slideshowProgressActive) {
        biltooLoadDbg("PATH slideshow active skip image-mode load path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
    }

    // Image mode: paint soft/LQIP from cache immediately (pixel swap only).
    if (role == LoadReplace && isImageMode()) {
        installImageModePendingTile(path);
        // Soft already on the item: only climb PreferCache; skip soft pool job.
        if (ImageItem *it = imageModeItemForPath(path)) {
            if (it->displayPixelLongEdge() > 0) {
                // Rapid ←/→: soft only. PreferCache after settle clears nav hot.
                if (m_slideshowNavHot) {
                    biltooLoadDbg("PATH soft-on-item skip climb (nav hot) path=%s edge=%d",
                                  qPrintable(QFileInfo(path).fileName()),
                                  it->displayPixelLongEdge());
                    return;
                }
                const QImage soft = it->displayImage();
                const QString pathCopy = path;
                // 16ms: let the soft repaint land one frame before PreferCache
                // schedules more GUI work (scheduleProbe/Pixels/Display).
                QTimer::singleShot(16, this, [this, pathCopy, soft]() {
                    if (!isImageMode() || classicPath() != pathCopy) {
                        return;
                    }
                    if (m_slideshowNavHot) {
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

    // Cold host: Soft→PreferCache→Full via PathRaster only (contract §1).
    if (role == LoadReplace && ImageCache::get(path).isNull()) {
        requestEscalateClimb(path, ThumtooCache::kGalleryLadderEdge);
    }

    // Rapid ←/→: soft schedule only — no native full / PreferCache until settle.
    if (role == LoadReplace && m_slideshowNavHot && isImageMode()) {
        biltooLoadDbg("PATH nav-hot skip classic decode path=%s",
                      qPrintable(QFileInfo(path).fileName()));
        return;
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
    // Snapshot session appearance for the worker (crop is id-keyed, not path).
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);

    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    if (qualityEdge > softEdge) {
        startDisplayQualityJob(guard, path, gen, roleInt, qualityEdge, sessionApp);
    }
}

void ImageView::scheduleClassicImageDecode(const QString &path, quint64 gen,
                                           LoadRole role)
{
    // Soft for fast paint. Image LoadReplace also starts Escalate + host native
    // (skipped during slideshow nav hot / active show).
    const QPointer<ImageView> guard(this);
    const int roleInt = static_cast<int>(role);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);

    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
    if (isImageMode() && !m_slideshowProgressActive && !m_slideshowNavHot
        && role == LoadReplace) {
        requestEscalateClimb(path, ThumtooCache::kImageLadderEdge);
        scheduleImageModeNativeDecodeOnce(path);
    }
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
        if (it.value().inflight > 0) {
            ++n;
        }
    }
    return n;
}

void ImageView::gallerySoftResetPath(const QString &path)
{
    m_gallerySoft.remove(path);
    if (m_pathRaster && !path.isEmpty()) {
        m_pathRaster->cancel(path);
    }
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

void ImageView::clearGalleryGaveUpIfClimbable(GallerySoftState &st, int have, int want)
{
    // Higher zoom/need or soft arrived — clear shortfall mirror so decode window
    // may schedule again. PathRasterService clears preferGaveUp when want rises.
    if (st.gaveUpWant > 0
        && (want > st.gaveUpWant || coversEdge(have, st.gaveUpWant))) {
        st.gaveUpWant = 0;
    }
}

void ImageView::syncGallerySoftMirrorFromPathRaster(const QString &path,
                                                    GallerySoftState &st)
{
    if (!m_pathRaster || path.isEmpty()) {
        return;
    }
    st.have = qMax(st.have, m_pathRaster->haveEdge(path));
    if (m_pathRaster->isGaveUp(path)) {
        const int prWant = m_pathRaster->wantEdge(path);
        st.gaveUpWant = qMax(st.gaveUpWant, prWant > 0 ? prWant : st.want);
    } else if (st.want > 0 && st.gaveUpWant > 0 && st.want > st.gaveUpWant) {
        // Zoom raised product need past mirrored plateau.
        st.gaveUpWant = 0;
    }
}

bool ImageView::gallerySoftScheduleBlocked(const GallerySoftState &st, int have,
                                           int want) const
{
    if (gallerySoftInflightCount() >= galleryDecodeConcurrency()) {
        return true;
    }
    // gaveUpWant is mirrored from PathRasterService in the decode window /
    // scheduleGalleryDecode; block soft-band retries when still short of soft max.
    const int softCap = ThumtooCache::kGalleryLadderEdge;
    // LQIP-only have must not be blocked by a mirrored PreferCache plateau.
    if (have >= 96 && !coversEdge(have, softCap)
        && st.gaveUpWant >= qMin(want, softCap)) {
        return true;
    }
    return false;
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

void ImageView::scheduleGalleryDecode(const QString &path)
{
    ASSERT_GUI_THREAD();
    if (!isGalleryMode() || path.isEmpty()) {
        return;
    }
    // Size-first still probes in the background, but never blocks decode:
    // provisional layout must still climb to the zoom-appropriate ladder edge.
    if (isProvisionalImageSize(path)) {
        scheduleImageSizeProbe(path);
    }
    GallerySoftState &st = m_gallerySoft[path];
    if (st.failed) {
        return;
    }
    // LQIP-only tiles may keep a stale inflight flag after a fast scroll; clear
    // so SoftDisplay can run again when the tile is still weak.
    if (st.inflight > 0) {
        if (st.have >= 128) {
            return;
        }
        clearGallerySoftInflight(st);
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

    if (!m_pathRaster) {
        return;
    }

    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/gallery: pathRaster ensure path need=%d have=%d\n",
                want, have);
    }

    markGallerySoftInflight(st, want);
    // Past overview: EscalateToFull so FocusFull + Full run (SoftDisplay used
    // to wait on PreferCache soft forever when zoomed).
    const auto climbPolicy =
        (want > ThumtooCache::kBatchOverviewEdge)
            ? PathRasterService::ClimbPolicy::EscalateToFull
            : PathRasterService::ClimbPolicy::SoftDisplay;
    m_pathRaster->ensure(path, want, logicalSizeForPath(path), climbPolicy);
    syncGallerySoftMirrorFromPathRaster(path, st);

    // Synchronous cache coverage: ensure may satisfy without async ladderReady.
    if (st.have > have) {
        const QImage img = ImageCache::get(path);
        if (!img.isNull()) {
            onImagePreviewLoaded(path, img, m_loadGeneration.load(),
                                 static_cast<int>(LoadAdd));
            st.have = qMax(st.have, ImageCache::longEdge(img));
        }
    }
    if (coversEdge(st.have, want)) {
        clearGallerySoftInflight(st);
        if (st.gaveUpWant <= want) {
            st.gaveUpWant = 0;
        }
        return;
    }
    if (m_pathRaster->isGaveUp(path)) {
        clearGallerySoftInflight(st);
        return;
    }
    if (!m_pathRaster->isClimbPending(path)) {
        // ensure scheduled nothing (thumtoo down / already settled policy) —
        // do not pin gallery inflight until the watchdog.
        clearGallerySoftInflight(st);
        return;
    }
    // Async: ladderReady -> applyGalleryLadderReady clears inflight via noteLadderDelivery.
}

void ImageView::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty()) {
        return;
    }
    // Central climb policy + ImageCache put. Slideshow installs via
    // PathRasterService::rasterImproved (no second direct call).
    if (m_pathRaster) {
        m_pathRaster->noteDelivery(path, maxEdge, image);
        // PreferCache BestAvailable / Full escalate is PathRasterService policy
        // (docs/THUMTOO_HOST_CONTRACT.md). Do not recover ad-hoc here.
    } else {
        if (!image.isNull()) {
            ImageCache::put(path, image);
        }
        if (m_slideshowProgressActive && !image.isNull()) {
            onSlideshowRasterReady(path, image);
        }
    }

    // Image mode: soft→sharp when full ImageLoader::load missed or is still
    // in flight. ladderReady used to return early for non-Gallery, so PDF /
    // page / archive PreferCache deliveries left the view stuck on the soft
    // thumbnail forever.
    if (isImageMode() && !image.isNull() && !m_slideshowProgressActive) {
        upgradeImageModeFromLadder(path, maxEdge, image);
    }

    // Workspace: was falling through to early return without installing samples.
    if (isWorkspaceMode() && !image.isNull()) {
        applyWorkspaceLadderReady(path, maxEdge, image);
    }

    // Crop may be open in Image or Workspace on a provisional sample.
    if (!image.isNull()) {
        maybeUpgradeCropFullRaster(path, image);
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
    // Ladder samples are raw (no session crop). Bake on a worker when needed —
    // materializeDisplay must not run on the GUI for multi-MP.
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);
    const bool needBake = SessionAppearance::hasContentAppearance(sessionApp)
        || !sessionApp.colorAdjust.isIdentity();
    if (needBake) {
        const QPointer<ImageView> guard(this);
        const quint64 gen = m_loadGeneration.load();
        const SessionAppearance::PixelKind kind =
            pixelKindForImageModeSample(path, image);
        QThreadPool::globalInstance()->start(
            [guard, path, image, sessionApp, gen, kind]() {
                ImageView *view = guard.data();
                if (!view || !view->matchesLoadGeneration(gen)) {
                    return;
                }
                QImage baked = prepareImageModeDisplaySample(
                    path, image, kind, &sessionApp);
                if (baked.isNull()) {
                    return;
                }
                // Capture expected path by value; resolve the view pointer on the
                // GUI slot so classicPath() is not evaluated on a null QPointer
                // (silences -Wnull-dereference on QString copy).
                QTimer::singleShot(0, view, [guard, path, baked, gen, kind]() {
                    ImageView *v = guard.data();
                    if (!v || !v->matchesLoadGeneration(gen)) {
                        return;
                    }
                    if (path != v->classicPath()) {
                        return;
                    }
                    // Already prepared — do not bake again in tryInstall.
                    (void)v->tryInstallImageModeSampleBaked(path, baked, kind);
                });
            },
            1);
        return;
    }
    // PathRasterService already recorded this delivery in onLadderReady.
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
        syncGallerySoftMirrorFromPathRaster(path, it.value());
    }

    // Debounce window rescan — avoid full setInterest on every tile delivery.
    scheduleGalleryDecodeWindowRefresh(150);
    emit statusChanged();
}


void ImageView::applyWorkspaceLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    ASSERT_GUI_THREAD();
    if (!isWorkspaceMode() || path.isEmpty() || image.isNull()) {
        return;
    }
    Q_UNUSED(maxEdge);
    // Same soft/display install as Gallery tiles — Workspace items share paths.
    onImagePreviewLoaded(path, image, m_loadGeneration.load(),
                         static_cast<int>(LoadAdd));
    ensureWorkspaceQualityClimb();
}

void ImageView::ensureWorkspaceQualityClimb()
{
    ASSERT_GUI_THREAD();
    if (!isWorkspaceMode() || !m_pathRaster || !m_scene) {
        return;
    }
    QList<ImageItem *> targets;
    for (QGraphicsItem *gi : m_scene->selectedItems()) {
        if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
            targets.append(ii);
        }
    }
    if (targets.isEmpty()) {
        // No selection: climb all on-canvas items (bounded).
        int n = 0;
        for (ImageItem *ii : m_items) {
            if (!ii || ii->path().isEmpty()) {
                continue;
            }
            targets.append(ii);
            if (++n >= 8) {
                break;
            }
        }
    }
    for (ImageItem *ii : targets) {
        if (!ii) {
            continue;
        }
        const QString path = ii->path();
        if (path.isEmpty()) {
            continue;
        }
        const int need = itemOnScreenNeedEdge(ii, /*allowHighRes=*/true);
        const int have = ii->displayPixelLongEdge();
        if (need <= 0 || coversEdge(have, need)) {
            continue;
        }
        m_pathRaster->ensure(path, need, logicalSizeForPath(path),
                             PathRasterService::ClimbPolicy::EscalateToFull);
        if (m_pathRaster->isGaveUp(path)
            && !coversEdge(ii->displayPixelLongEdge(), need)) {
            scheduleImageModeNativeDecodeOnce(path);
        }
    }
}

void ImageView::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    ASSERT_GUI_THREAD();
    if (path.isEmpty() || m_imageModeNativeDecodePaths.contains(path)) {
        return;
    }
    m_imageModeNativeDecodePaths.insert(path);
    const quint64 gen = m_loadGeneration.load();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen]() {
        ASSERT_NOT_GUI_THREAD();
        const QImage decoded = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, decoded, gen]() {
                ImageView *const host = guard.data();
                if (!host) {
                    return;
                }
                if (!decoded.isNull()) {
                    ImageCache::put(path, decoded);
                }
                if (host->isImageMode()) {
                    if (gen != host->m_loadGeneration.load()) {
                        return;
                    }
                    if (!decoded.isNull()) {
                        (void)host->tryInstallImageModeSample(path, decoded);
                    }
                } else if (host->isWorkspaceMode() && !decoded.isNull()) {
                    host->onImagePreviewLoaded(
                        path, decoded, host->m_loadGeneration.load(),
                        static_cast<int>(LoadAdd));
                }
            },
            Qt::QueuedConnection);
    });
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

    // Gallery / Workspace: install or upgrade samples for this path.
    // SoftPreview tiles must accept PreferCache/Full upgrades (hasDecodedPixels
    // is false for soft). FullSource tiles still reject SoftPreview demotion in
    // canAcceptDisplaySample.
    const int incoming = ImageCache::longEdge(image);
    bool gallerySizeChanged = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        // FullSource already native-class: only accept strict upgrades.
        if (item->hasDecodedPixels() && !item->shouldUpgradeDisplayTo(incoming)) {
            continue;
        }
        if (!item->hasDecodedPixels() && item->hasDisplayPixels()
            && !item->shouldUpgradeDisplayTo(incoming)) {
            continue;
        }
        const SessionAppearance::PixelKind kind =
            (incoming > ThumtooCache::kGalleryLadderEdge)
                ? SessionAppearance::PixelKind::FullSource
                : SessionAppearance::PixelKind::SoftPreview;
        const QSize before = item->imageSize();
        installDisplayPixels(item, image, kind, item->sessionId());
        if (item->imageSize() != before) {
            gallerySizeChanged = true;
        }
        item->update();
        if (m_scene) {
            m_scene->update(item->sceneBoundingRect());
        }
    }
    if (gallerySizeChanged && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (viewport()) {
        viewport()->update();
    }
    // Workspace soft land: start PreferCache→Full (was never ensured until select).
    if (isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
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
    if (m_gallerySizeResolveActive || m_galleryDeferPopulate) {
        return;
    }
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
    if (m_gallerySizeResolveActive || m_galleryDeferPopulate) {
        return;
    }
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
    if (isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
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
        applyImageModeFraming(item);
    }
    syncImageModeSceneRect(item);
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
    // Preserve sticky pan across the wipe (soft→full or cold replace).
    if (!m_items.isEmpty()) {
        if (m_items.first()->path() != path) {
            captureStickyPanAnchor(m_items.first());
        } else if (m_stickyZoomEnabled
                   && m_stickyZoomKind != StickyZoomKind::Fit) {
            // Same path rebuild: keep looking where we are now.
            captureStickyPanAnchor(m_items.first());
        }
    }
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
    requestEscalateClimb(path, wantEdge);
}

void ImageView::requestEscalateClimb(const QString &path, int wantEdge)
{
    if (!m_pathRaster || path.isEmpty() || m_slideshowNavHot) {
        return;
    }
    const int edge = cappedDisplayEdgeForPath(
        path, wantEdge > 0 ? wantEdge : ThumtooCache::kImageLadderEdge);
    biltooLoadDbg("escalateClimb(service) path=%s edge=%d",
                  qPrintable(QFileInfo(path).fileName()), edge);
    m_pathRaster->ensure(path, edge, logicalSizeForPath(path),
                         PathRasterService::ClimbPolicy::EscalateToFull);
}

void ImageView::installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                             const QImage &image,
                                             SessionAppearance::PixelKind kind)
{
    if (!item || image.isNull()) {
        return;
    }
    // Same rules as every other attach: accept → materialize → attachDisplaySample.
    installDisplayPixels(item, image, kind, item->sessionId() != kInvalidSessionImageId
                                             ? item->sessionId()
                                             : m_currentSessionId);
    m_lastLoadError.clear();
    rememberSizeFromDecode(path, image);
    if (viewport()) {
        viewport()->update();
    }
}


int ImageView::cappedDisplayEdgeForPath(const QString &path, int wantEdge) const
{
    // Ladder steps are discrete (…1024, 2048). Never request past the known
    // native long edge — PreferCache cannot invent pixels, and the HUD must
    // not claim "→2048" for a 1920×1080 photo.
    int edge = wantEdge > 0 ? wantEdge : ThumtooCache::kImageLadderEdge;
    edge = qMin(edge, ThumtooCache::kImageLadderEdge);
    const QSize logical = logicalSizeForPath(path);
    if (isPositiveSize(logical) && !isProvisionalImageSize(path)) {
        const int native = qMax(logical.width(), logical.height());
        if (native > 0) {
            edge = qMin(edge, native);
        }
    }
    // Snap up only within the remaining budget (ceil then clamp to native again).
    edge = ThumtooCache::ceilLadderEdge(edge);
    if (isPositiveSize(logical) && !isProvisionalImageSize(path)) {
        const int native = qMax(logical.width(), logical.height());
        if (native > 0) {
            edge = qMin(edge, native);
        }
    }
    return qMax(1, edge);
}

QImage ImageView::fullRasterForEdit(const QString &path) const
{
    if (path.isEmpty()) {
        return {};
    }
    const QImage cached = ImageCache::get(path);
    if (!cached.isNull() && sampleCoversNativeLogical(path, cached)) {
        return cached;
    }
    // Cold path: decode on this thread (crop/Workspace enter). Prefer ImageCache
    // after Image-mode climb so this is rare. Put result for peers / re-enter.
    const QImage full = ImageLoader::load(path);
    if (!full.isNull()) {
        ImageCache::put(path, full);
    }
    return full;
}

bool ImageView::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    // Soft and overview (≤1024) are never final for Image/Workspace high-res.
    // Provisional / unknown native: only ≥ kImageLadderEdge (2048) is enough.
    // Known native: ~90% of true long edge.
    const int incoming = ImageCache::longEdge(image);
    if (incoming <= 0) {
        return false;
    }
    if (incoming <= ThumtooCache::kBatchOverviewEdge) {
        return false;
    }
    const QSize logical = logicalSizeForPath(path);
    if (!isPositiveSize(logical) || isProvisionalImageSize(path)) {
        return incoming >= ThumtooCache::kImageLadderEdge;
    }
    const int native = qMax(logical.width(), logical.height());
    return coversEdge(incoming, native);
}

void ImageView::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    if (m_pathRaster && !path.isEmpty()) {
        m_pathRaster->noteDelivery(path, requestEdge, sample);
    }
}

void ImageView::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    if (path.isEmpty() || m_slideshowNavHot || !m_pathRaster) {
        return;
    }
    if (m_slideshowProgressActive) {
        return;
    }
    if (!sample.isNull() && sampleCoversNativeLogical(path, sample)) {
        return;
    }

    const int need = imageModeOnScreenNeedEdge();
    const int have = sample.isNull() ? 0 : ImageCache::longEdge(sample);
    int climbTo = ThumtooCache::kBatchOverviewEdge;
    if (need > 0) {
        climbTo = qMax(climbTo, need);
    }
    if (climbTo < ThumtooCache::kGalleryLadderEdge) {
        climbTo = ThumtooCache::kGalleryLadderEdge;
    }
    climbTo = cappedDisplayEdgeForPath(path, climbTo);
    if (have > 0 && coversEdge(have, climbTo)) {
        return;
    }
    // PreferCache BestAvailable → Full is PathRasterService policy (contract §4).
    biltooLoadDbg("imageModeClimb(service) path=%s climbTo=%d have=%d need=%d",
                  qPrintable(QFileInfo(path).fileName()), climbTo, have, need);
    m_pathRaster->ensure(path, climbTo, logicalSizeForPath(path),
                         PathRasterService::ClimbPolicy::EscalateToFull);
    // Full is async. If terminal or nothing pending and still short, host native.
    if (!coversEdge(m_pathRaster->haveEdge(path), climbTo)
        && (m_pathRaster->isGaveUp(path) || !m_pathRaster->isClimbPending(path))) {
        scheduleImageModeNativeDecodeOnce(path);
    }
}

bool ImageView::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    if (!isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    const SessionAppearance::PixelKind kind = pixelKindForImageModeSample(path, image);
    // Full/native/PreferCache samples must bake session crop on a worker.
    // installDisplayPixels only materializes ≤512 on the GUI — larger raw
    // installs were replacing a correctly cropped soft frame with an uncropped
    // full frame (Gallery→Image crop "lost").
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);
    const bool needBake = SessionAppearance::hasContentAppearance(sessionApp)
        || !sessionApp.colorAdjust.isIdentity();
    if (needBake && ImageCache::longEdge(image) > ThumtooCache::kGalleryLadderEdge) {
        const QPointer<ImageView> guard(this);
        const quint64 gen = m_loadGeneration.load();
        const QImage raw = image;
        QThreadPool::globalInstance()->start([guard, path, raw, kind, sessionApp, gen]() {
            ASSERT_NOT_GUI_THREAD();
            QImage baked = prepareImageModeDisplaySample(path, raw, kind, &sessionApp);
            if (!guard || baked.isNull()) {
                return;
            }
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, path, baked, kind, gen]() {
                    ImageView *const host = guard.data();
                    if (!host || gen != host->m_loadGeneration.load()
                        || !host->isImageMode() || path != host->classicPath()) {
                        return;
                    }
                    (void)host->tryInstallImageModeSampleBaked(path, baked, kind);
                },
                Qt::QueuedConnection);
        });
        // Still climb using raw size (want/need), not blocked by bake.
        if (ImageItem *cur = imageModeItemForPath(path)) {
            const int incoming = ImageCache::longEdge(image);
            const int painted = cur->displayPixelLongEdge();
            const bool noUpgrade = incoming > 0 && painted > 0 && incoming <= painted;
            if (kind == SessionAppearance::PixelKind::SoftPreview
                || !sampleCoversNativeLogical(path, image)) {
                if (!(noUpgrade && m_pathRaster && m_pathRaster->isGaveUp(path))) {
                    ensureImageModeQualityClimb(path, image);
                } else {
                    const int need = imageModeOnScreenNeedEdge();
                    if (need > painted) {
                        scheduleImageModeNativeDecodeOnce(path);
                    }
                }
            }
        }
        return true;
    }
    return tryInstallImageModeSampleBaked(path, image, kind);
}

bool ImageView::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    if (!isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
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
        // Climb while soft or short of native.
        const int incoming = ImageCache::longEdge(image);
        const int painted = cur->displayPixelLongEdge();
        const bool noUpgrade = incoming > 0 && painted > 0 && incoming <= painted;
        if (kind == SessionAppearance::PixelKind::SoftPreview
            || !sampleCoversNativeLogical(path, image)) {
            if (!(noUpgrade && m_pathRaster && m_pathRaster->isGaveUp(path))) {
                ensureImageModeQualityClimb(path, image);
            } else {
                // Terminal PreferCache/Full shortfall at same edge as painted —
                // still need host native when on-screen need exceeds that.
                const int need = imageModeOnScreenNeedEdge();
                if (need > painted) {
                    scheduleImageModeNativeDecodeOnce(path);
                }
            }
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

    // Zoom-in: PathRasterService Soft→PreferCache→Full (contract EscalateToFull).
    scheduleImageModePreferCacheClimb(path, need);
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
