// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "tilelod/tile_lod_registry.hpp"
#include "gallerysoftsm.h"
#include "viewtransform.h"
#include "displayedgepolicy.h"
#include "softdisplaypolicy.h"
#include <QVarLengthArray>
#include "ttfp_trace.h"

#include <algorithm>
#include "displayquality.h"
#include "coloradjust.h"

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
#include "biltoo_logging.h"
#include "tile_load_coordinator.h"
#include "tilelod/tile_lod_controller.hpp"

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

/**
 * Worker-side: bake durable content appearance and clamp for display install.
 * Must not run on the GUI — materialize + scale of multi-MP samples is the
 * ←/→ hitch when done in installDisplayPixels.
 */
// prepareImageModeDisplaySample removed: workers must return host-raw only.
// GUI installDisplayPixels is the sole materialize site (see CONTENT_PIPELINE
// install invariant). Baking in the worker + put/materialize on the GUI
// double-applied crop and polluted ImageCache with content-baked samples.

} // namespace

int ImageView::pathOrderOccurrences(const QString &path) const
{
    // View-local multiplicity only (Gallery pack / LoadAdd).
    // clearPathOrder() zeros the book without wiping SessionDocument — consulting
    // the document here would recreate session tiles on a blank Workspace.
    return m_pathOrderBook.countPathOccurrences(path);
}

WorkspaceItemState ImageView::appearanceForNewImageModeItem(const QString &path)
{
    // Prefer stable session-image id appearance; path map is legacy only.
    //
    // Image mode LoadReplace: the sole canvas item is the current session
    // image, so m_sessionId.currentIdValue() identifies it correctly.
    //
    // Gallery / Workspace LoadAdd must not call this: each tile is bound to
    // its own session id *after* creation. Applying m_sessionId.currentIdValue() here
    // would bake the navigated image's crop into every newly decoded tile.
    if (m_sessionId.hasCurrentId()) {
        seedSessionAppearanceFromState(m_sessionId.currentIdValue(), path);
        if (const WorkspaceItemState *sit = appearance().get(m_sessionId.currentIdValue())) {
            return *sit;
        }
        // Bound session image with no appearance entry = full frame, no path fallback.
        return {};
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_itemStateBook.get(path)) {
        return *st;
    }
    return {};
}

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    // @p image is always host-raw (workers no longer bake). Size-first ctor;
    // never QPixmap::fromImage of multi-MP in ImageItem(path, image).
    WorkspaceItemState app;
    if (applyStoredSessionCrop && isImageMode()) {
        // Always attempt seed from path XDG when bound. The old gate
        // !(haveId && !appearance().get(id)) *skipped* seed when the slot was
        // empty — which is exactly when durable rotate/flip must be loaded
        // after restart. appearanceForNewImageModeItem seeds then returns
        // identity only if XDG has nothing.
        if (m_sessionId.hasCurrentId()
            || m_itemStateBook.contains(path)) {
            app = appearanceForNewImageModeItem(path);
        }
    }
    // Logical size only from probe / map — never sample (LQIP/soft) dims.
    QSize native = layoutSizeForPath(path, QImage());
    if (isProvisionalImageSize(path)
        || !isPositiveSize(native) || native.width() <= 1 || native.height() <= 1) {
        // Cold: 1×1 until sizeReady; soft install must not invent geometry.
        native = QSize(1, 1);
        scheduleImageSizeProbe(path);
    }
    QSize intrinsic = ContentXform::layoutSize(native, app);
    if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
        intrinsic = QSize(1, 1);
    }

    auto *item = new ImageItem(path, intrinsic);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    registerItemDisplaySurface(item);

    // @p image is host-raw. Sole materialize site is installDisplayPixels.
    const bool wantBake = SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity();
    if (wantBake) {
        // Seed item chrome so wantAppearanceForItem can merge if the store slot
        // is still empty (bound id with no entry yet).
        item->setContentHFlip(app.contentHFlip);
        item->setContentVFlip(app.contentVFlip);
        item->setSessionCrop(app.hasCrop, app.cropRect);
        item->setColorAdjustmentsRecord(app.colorAdjust);
        const SessionImageId sid = isImageMode()
            ? m_sessionId.currentIdValue()
            : kInvalidSessionImageId;
        const int hostEdge = ImageCache::longEdge(image);
        const auto kind = (hostEdge > ThumtooCache::kGalleryLadderEdge)
            ? SessionAppearance::PixelKind::FullSource
            : SessionAppearance::PixelKind::SoftPreview;
        installDisplayPixels(item, image, kind, sid);
    } else {
        if (!path.isEmpty()) {
            ImageCache::put(path, image);
        }
        // Always installDisplayPixels so seed + materialize run. Skipping that
        // for Image-mode Soft (setPreviewImage) left durable orient unapplied.
        const int hostEdge = ImageCache::longEdge(image);
        const auto kind = (hostEdge > ThumtooCache::kGalleryLadderEdge)
            ? SessionAppearance::PixelKind::FullSource
            : SessionAppearance::PixelKind::SoftPreview;
        const SessionImageId sid = isImageMode()
            ? m_sessionId.currentIdValue()
            : item->sessionId();
        installDisplayPixels(item, image, kind, sid);
    }
    return item;
}


void ImageView::seedSessionAppearancesFromPaths(const QStringList &paths,
                                                   const QVector<SessionImageId> &ids)
{
    // Fresh session: allow seed again for new ids (old set cleared on invalidate).
    const int n = ViewTransform::pairCount(paths.size(), ids.size());
    for (int i = 0; i < n; ++i) {
        appearance().clearSeedAttempted(ids.at(i));
    }
    // Small sessions: fine on GUI (few stats). Large sessions: locatorId +
    // appearance SQLite used to run O(n) on the GUI during open and freeze the
    // event loop while the HUD still said "Reading file info…".
    if (n <= 24) {
        for (int i = 0; i < n; ++i) {
            if (ids.at(i) == kInvalidSessionImageId || paths.at(i).isEmpty()) {
                continue;
            }
            seedSessionAppearanceFromState(ids.at(i), paths.at(i));
        }
        return;
    }
    QVector<SessionImageId> idsCopy = ids.mid(0, n);
    QStringList pathsCopy = paths.mid(0, n);
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, pathsCopy, idsCopy]() {
        struct Hit {
            SessionImageId sid = kInvalidSessionImageId;
            QString path;
            ThumtooCache::StoredContentAppearance stored;
        };
        QVector<Hit> hits;
        hits.reserve(pathsCopy.size());
        // Every examined sid must be marked attempted on the GUI (including
        // load miss / identity) so wantAppearanceForItem does not re-drive
        // locatorId on every paint.
        QVector<SessionImageId> attempted;
        attempted.reserve(pathsCopy.size());
        for (int i = 0; i < pathsCopy.size(); ++i) {
            if (idsCopy.at(i) == kInvalidSessionImageId || pathsCopy.at(i).isEmpty()) {
                continue;
            }
            attempted.push_back(idsCopy.at(i));
            ThumtooCache::StoredContentAppearance stored;
            if (!ThumtooCache::loadContentAppearance(pathsCopy.at(i), &stored)) {
                continue;
            }
            if (stored.isIdentity()) {
                continue;
            }
            hits.push_back(Hit{idsCopy.at(i), pathsCopy.at(i), stored});
        }
        if (hits.isEmpty() && attempted.isEmpty()) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, hits, attempted]() {
            ImageView *host = guard.data();
            if (!host) {
                return;
            }
            for (const SessionImageId sid : attempted) {
                host->markAppearanceSeedAttempted(sid);
            }
            for (const Hit &h : hits) {
                host->applyStoredContentAppearanceSeed(h.sid, h.path, h.stored);
            }
        }, Qt::QueuedConnection);
    });
}

void ImageView::seedSessionAppearanceFromState(SessionImageId sid, const QString &path)
{
    if (sid == kInvalidSessionImageId || path.isEmpty()) {
        return;
    }
    // One attempt per session id — archive/miss paths must not re-hit locatorId
    // on every paint via wantAppearanceForItem.
    if (appearance().seedAttempted(sid)) {
        return;
    }
    appearance().markSeedAttempted(sid);
    ThumtooCache::StoredContentAppearance stored;
    if (!ThumtooCache::loadContentAppearance(path, &stored)) {
        return;
    }
    if (stored.isIdentity()) {
        return;
    }
    applyStoredContentAppearanceSeed(sid, path, stored);
}

void ImageView::markAppearanceSeedAttempted(SessionImageId sid)
{
    if (sid != kInvalidSessionImageId) {
        appearance().markSeedAttempted(sid);
    }
}

void ImageView::applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                                 const ThumtooCache::StoredContentAppearance &stored)
{
    if (sid == kInvalidSessionImageId || path.isEmpty() || stored.isIdentity()) {
        return;
    }
    // Worker path may not have marked attempted yet; mark here so paint does not
    // re-drive locatorId via wantAppearanceForItem.
    appearance().markSeedAttempted(sid);
    if (appearance().contains(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (const WorkspaceItemState *cur = appearance().get(sid)) {
            if (SessionAppearance::hasContentAppearance(*cur)) {
                return;
            }
        }
    }
    WorkspaceItemState seed;
    seed.sessionId = sid;
    seed.path = path;
    // Orient/flip/grade only. Crop is per SessionImageId — never seed from
    // path-keyed XDG (duplicates share a path; last crop would leak).
    seed.contentHFlip = stored.contentHFlip;
    seed.contentVFlip = stored.contentVFlip;
    seed.contentQuarterTurns = stored.contentQuarterTurns;
    if (stored.hasGrade) {
        // Durable gradeGamma is percent (100 = 1.0); 0 contrast/sat = identity 100.
        seed.colorAdjust = ColorAdjustments::fromDurableGrade(
            stored.gradeBrightness, stored.gradeContrast, stored.gradeSaturation,
            stored.gradeHue, stored.gradeGamma, stored.gradeInvert);
    }
    appearance().set(sid, seed);
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
                  : m_sessionId.currentIdValue();
    }
    installDisplayPixels(item, pixels, kind, sid);
    preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
}

WorkspaceItemState ImageView::wantAppearanceForItem(const ImageItem *item,
                                                      SessionImageId sid) const
{
    WorkspaceItemState want;
    if (!item) {
        return want;
    }
    SessionImageId id = sid;
    if (id == kInvalidSessionImageId) {
        id = item->sessionId();
    }
    if (id == kInvalidSessionImageId && isImageMode()) {
        id = m_sessionId.currentIdValue();
    }
    if (id != kInvalidSessionImageId) {
        if (const WorkspaceItemState *app = appearance().get(id)) {
            want = *app;
        }
        // Cold open / ←→: id slot often empty until first seed. Path XDG holds
        // durable rotate/flip/grade — pull it before materialize or we paint
        // unoriented host forever.
        if (!SessionAppearance::hasContentAppearance(want)
            && want.colorAdjust.isIdentity()
            && !item->path().isEmpty()) {
            const_cast<ImageView *>(this)->seedSessionAppearanceFromState(
                id, item->path());
            if (const WorkspaceItemState *app = appearance().get(id)) {
                want = *app;
            }
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_itemStateBook.get(item->path())) {
            want = *st;
        }
    }
    const ContentXform::Value applied =
        item->hasAppliedContentXform() ? item->appliedContentXform()
                                       : ContentXform::Value{};
    SessionAppearance::mergeAppliedAndLiveFlags(
        want,
        item->hasAppliedContentXform() ? &applied : nullptr,
        item->contentHFlip(), item->contentVFlip(),
        item->sessionHasCrop(), item->sessionCropRect());
    return want;
}

DisplaySurface::State ImageView::displaySurfaceStateForItem(const ImageItem *item,
                                                            int hostLongEdge,
                                                            bool climbPending) const
{
    return m_displayPipeline.displaySurfaceStateForItem(item, hostLongEdge, climbPending);
}

bool ImageView::applyDisplaySurfaceAction(ImageItem *item,
                                          const DisplaySurface::Action &act,
                                          const QImage &hostSample,
                                          int fallbackNeedEdge,
                                          PathRasterService::ClimbPolicy climbPolicy)
{
    return m_displayPipeline.applyDisplaySurfaceAction(item, act, hostSample, fallbackNeedEdge, climbPolicy);
}

bool ImageView::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    return m_displayPipeline.canAcceptDisplaySample(item, pixels, kind);
}

void ImageView::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    m_displayPipeline.installDisplayPixels(item, pixels, kind, sid);
}

ImageItem *ImageView::createPlaceholderItem(const QString &path, const QSize &intrinsicSize)
{
    // Defer-populate only: size-resolve for Fill layouts must still create
    // placeholders so soft can install. applyLayout stays deferred until
    // finishGallerySizeResolve. Blocking on gallerySizeResolveActive() left
    // m_items empty until a manual relayout (and never for pure Fill open).
    if (m_gallerySoftBook.isDeferPopulate()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, intrinsicSize);
    applyItemModeFlags(item);
    m_scene->addItem(item);
    m_items.append(item);
    registerItemDisplaySurface(item);
    return item;
}

void ImageView::bindImageModeSessionCursor(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Image-mode crop/flip targets the matching Workspace session slot.
    if (m_sessionId.hasCurrentId()) {
        item->setSessionId(m_sessionId.currentIdValue());
    }
    if (m_sessionId.currentIndex() >= 0) {
        item->setSessionIndex(m_sessionId.currentIndex());
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
    bool unused = false;
    return resolveImageModePendingPixels(path, preview, &unused);
}

QImage ImageView::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview,
                                                bool *displayReadyOut) const
{
    // Image-mode ←/→ hot path: process memory only (no sync thumtoo IPC).
    //
    // Soft sources, in order:
    // 1) Explicit preview / slideshow raster  → host-raw candidate
    // 2) ImageCache                          → host-raw
    // 3) Stashed Gallery tile soft:
    //    - display-ready only when same SessionImageId and applied == store want
    //      (already content-baked; must not ImageCache::put or re-materialize)
    //    - otherwise host-raw only when the tile has no content bake. Soft baked
    //      for a *different* id is skipped — not unoriented host, must not be
    //      materialize()'d again (double-bake).
    if (displayReadyOut) {
        *displayReadyOut = false;
    }
    if (!preview.isNull()) {
        return preview;
    }
    QImage pixels = m_slideshow.slideshowRaster(path);
    // Filmstrip often has Soft while ImageCache only has size-probe LQIP (or
    // LRU-evicted the soft). Prefer strip / shared host sample first.
    if (pixels.isNull() && m_imageModeSoftProvider) {
        bool ready = false;
        pixels = m_imageModeSoftProvider(path, m_sessionId.currentIdValue(), &ready);
        if (!pixels.isNull()) {
            if (displayReadyOut) {
                *displayReadyOut = ready;
            }
            return pixels;
        }
    }
    // Best host in process memory (any edge). Do not require Soft ladder first —
    // filmstrip decode edge may be 128–256 and still beat LQIP.
    if (pixels.isNull()) {
        pixels = ImageCache::get(path);
    }
    // LQIP only if already in the durable/process cache — never request encode.
    if (pixels.isNull()) {
        pixels = ThumtooCache::cachedLqipImage(path);
        if (!pixels.isNull()) {
            ImageCache::put(path, pixels);
        }
    }
    if (!pixels.isNull()) {
        return pixels;
    }

    WorkspaceItemState want;
    if (m_sessionId.hasCurrentId()) {
        if (const WorkspaceItemState *st = appearance().get(m_sessionId.currentIdValue())) {
            want = *st;
        }
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);

    for (ImageItem *cand : m_gallery.stashedItems()) {
        if (!cand || cand->path() != path || !cand->hasDisplayPixels()) {
            continue;
        }
        pixels = cand->displayImage();
        if (pixels.isNull()) {
            continue;
        }
        const bool sameId = (m_sessionId.hasCurrentId()
                             && cand->sessionId() == m_sessionId.currentIdValue());
        const bool hasApplied = cand->hasAppliedContentXform();
        const ContentXform::Value applied = hasApplied
            ? cand->appliedContentXform()
            : ContentXform::Value{};
        // Display-ready: same session row and bake already matches store want.
        if (sameId && hasApplied && ContentXform::equal(applied, wantX)) {
            if (displayReadyOut) {
                *displayReadyOut = true;
            }
            return pixels;
        }
        // Host-raw: only unbaked / identity soft (safe to materialize with want).
        if (!hasApplied || ContentXform::equal(applied, ContentXform::Value{})) {
            return pixels;
        }
        // Baked for another id or mismatched want — skip.
    }
    return {};
}

void ImageView::setImageModeSoftProvider(ImageModeSoftProvider provider)
{
    m_imageModeSoftProvider = std::move(provider);
}

void ImageView::installImageModePendingTile(const QString &path, const QImage &preview)
{
    m_displayPipeline.installImageModePendingTile(path, preview);
}

void ImageView::scheduleImageLoad(const QString &path, LoadRole role)
{
    m_displayPipeline.scheduleImageLoad(path, static_cast<int>(role));
}

bool ImageView::tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen)
{
    return m_displayPipeline.tryDeliverReplaceFromSlideshowRaster(path, gen);
}

void ImageView::scheduleSlideshowReplaceDecode(const QString &path, quint64 gen,
                                               LoadRole role)
{
    m_displayPipeline.scheduleSlideshowReplaceDecode(path, gen, static_cast<int>(role));
}

void ImageView::scheduleClassicImageDecode(const QString &path, quint64 gen,
                                           LoadRole role)
{
    m_displayPipeline.scheduleClassicImageDecode(path, gen, static_cast<int>(role));
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
        ViewTransform::chebyshev(a, b) * devicePixelRatioF();
    return DisplayEdgePolicy::needEdgeFromScreenLongPx(longPx, allowHighRes);
}

int ImageView::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    // Visible tiles: full on-screen need. Off-screen / idle: soft band only.
    return itemOnScreenNeedEdge(item, allowHighRes);
}



void ImageView::gallerySoftResetPath(const QString &path)
{
    m_displayPipeline.gallerySoftResetPath(path);
}

void ImageView::gallerySoftResetAll()
{
    m_displayPipeline.gallerySoftResetAll();
}

int ImageView::galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const
{
    return m_displayPipeline.galleryHaveEdgeFromItems(path, anyFullOut);
}

void ImageView::scheduleGalleryDecode(const QString &path)
{
    m_displayPipeline.scheduleGalleryDecode(path);
}

void ImageView::onLadderReady(const QString &path, int maxEdge, const QImage &image)
{
    m_displayPipeline.onLadderReady(path, maxEdge, image);
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
    m_displayPipeline.upgradeImageModeFromLadder(path, maxEdge, image);
}

void ImageView::applyGalleryLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    m_displayPipeline.applyGalleryLadderReady(path, maxEdge, image);
}

void ImageView::applyWorkspaceLadderReady(const QString &path, int maxEdge,
                                          const QImage &image)
{
    m_displayPipeline.applyWorkspaceLadderReady(path, maxEdge, image);
}

void ImageView::ensureWorkspaceQualityClimb()
{
    m_displayPipeline.ensureWorkspaceQualityClimb();
}


void ImageView::scheduleImageModeNativeDecodeOnce(const QString &path)
{
    m_displayPipeline.scheduleImageModeNativeDecodeOnce(path);
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
    if (m_slideshow.hud().isProgressActive()) {
        m_slideshow.onSlideshowRasterReady(path, image);
    }

    // Replace navigations: drop superseded previews.
    if (role == LoadReplace) {
        if (generation != m_displayPipeline.loadGate().generation() || path != classicPath()) {
            return;
        }
        if (isImageMode()) {
            // Same install + climb policy as completeLoadReplace / ladderReady.
            (void)tryInstallImageModeSample(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }

    const int incoming = ImageCache::longEdge(image);

    // Gallery: LQIP placeholder only. Soft PreferCache deliveries must not
    // climb Gallery cells (filmstrip soft used SoftDisplay here).
    if (isGalleryMode()) {
        if (incoming > 0 && incoming <= DisplayQuality::kLqipMaxEdge) {
            for (ImageItem *item : m_items) {
                if (!item || item->path() != path || item->hasDisplayPixels()) {
                    continue;
                }
                installDisplayPixels(item, image,
                                     SessionAppearance::PixelKind::SoftPreview,
                                     item->sessionId());
            }
            if (viewport()) {
                viewport()->update();
            }
        }
        return;
    }

    // Workspace: DisplaySurface::decide per item.
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path) {
            continue;
        }
        const bool climbPending =
            m_pathRaster && m_pathRaster->isClimbPending(path);
        syncItemDisplaySurface(item, incoming, climbPending);
        DisplaySurface::State ds =
            displaySurfaceStateForItem(item, incoming, climbPending);
        const DisplaySurface::SurfaceId sid =
            static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
        const DisplaySurface::Action act =
            (sid != DisplaySurface::kInvalidSurfaceId)
                ? m_displayPipeline.displaySurfaces().evaluate(sid)
                : DisplaySurface::decide(ds);
        const auto pol =
            (!ThumtooCache::hasDurableTilesKnown(path))
                ? PathRasterService::ClimbPolicy::EscalateToFull
                : PathRasterService::ClimbPolicy::SoftDisplay;
        (void)applyDisplaySurfaceAction(item, act, image, ds.needEdge, pol);
    }
    if (viewport()) {
        viewport()->update();
    }
    if (isWorkspaceMode()) {
        ensureWorkspaceQualityClimb();
    }
}

bool ImageView::takePendingRestoreState(const QString &path, WorkspaceItemState *out)
{
    return m_displayPipeline.loadGate().takePendingRestoreForPath(path, out);
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
        if (const WorkspaceItemState *it = appearance().get(state.sessionId)) {
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
    // Host is in ImageCache / item. Materialize store want (soft stand-in +
    // async multi-MP). Do not bake chrome-only on multi-MP — cannot
    // bake crop on the GUI and used to claim applied == want without pixels.
    if (SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity()) {
        rematerializeItemContent(item, app);
    }
    applyState(item, app);
    if (!m_layout.isFreeForm()
        && !(isGalleryMode() && m_galleryRelayoutSuppress.active())) {
        applyLayout(GalleryPackReason::SessionMutate);
    }
    emit statusChanged();
    emit workspacePathsChanged();
}

void ImageView::finishLoadAddStatus(bool refreshGalleryWindow)
{
    m_displayPipeline.finishLoadAddStatus(refreshGalleryWindow);
}

bool ImageView::acceptPendingLoadAdd(const QString &path, quint64 generation)
{
    return m_displayPipeline.acceptPendingLoadAdd(path, generation);
}

void ImageView::handleLoadAddDecodeFailure(const QString &path)
{
    m_displayPipeline.handleLoadAddDecodeFailure(path);
}

void ImageView::fillStashedItemsForPath(const QString &path, const QImage &image)
{
    m_displayPipeline.fillStashedItemsForPath(path, image);
}

void ImageView::reassertPendingBindPlacement(const QString &path)
{
    m_displayPipeline.reassertPendingBindPlacement(path);
}

void ImageView::claimUnboundItemsForPendingBinds(const QString &path, const QImage &image)
{
    m_displayPipeline.claimUnboundItemsForPendingBinds(path, image);
}

int ImageView::fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                              bool *sizeChangedOut)
{
    return m_displayPipeline.fillLiveItemsWithDecodedPixels(path, image, sizeChangedOut);
}

void ImageView::createMissingLoadAddItems(const QString &path, const QImage &image,
                                          int have, int wanted)
{
    m_displayPipeline.createMissingLoadAddItems(path, image, have, wanted);
}

void ImageView::applyLoadAddLayoutAfterMembership(bool sizeChanged)
{
    m_displayPipeline.applyLoadAddLayoutAfterMembership(sizeChanged);
}

void ImageView::completeLoadAdd(const QString &path, const QImage &image, quint64 generation)
{
    m_displayPipeline.completeLoadAdd(path, image, generation);
}

void ImageView::applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path)
{
    if (!item || path.isEmpty()) {
        return;
    }
    // Content 90°/flip/crop are materialize()'d in createItemFromImage when want is set.
    // Legacy unbaked flips only if content flags not used yet.
    const WorkspaceItemState *st = m_itemStateBook.get(path);
    if (!st) {
        return;
    }
    if (!st->contentHFlip && !st->contentVFlip) {
        item->setItemHFlip(st->hFlip);
        item->setItemVFlip(st->vFlip);
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
    if (m_slideshow.hud().isProgressActive() && m_slideshow.settings().isMotionOff()) {
        m_slideshow.applySlideshowZoomFraming(item);
    } else if (!m_slideshow.hud().isProgressActive()) {
        applyImageModeFraming(item);
    }
    syncImageModeSceneRect(item);
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    m_slideshow.maybeStartSlideshowMotion();
    if (m_slideshow.hud().isProgressActive() && !m_slideshow.settings().isMotionOff()
        && !m_slideshow.dwell().isMotionActive()) {
        m_slideshow.applySlideshowZoomFraming(item);
    }
    if (m_slideshow.hud().isProgressActive()) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        m_slideshow.setSlideshowPhase(path, QString(), -1.0);
    }
}

void ImageView::installImageModeReplaceItem(const QString &path, const QImage &image)
{
    m_displayPipeline.installImageModeReplaceItem(path, image);
}

void ImageView::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's LoadAdd.
    if (!m_items.isEmpty()
        || !m_bindBook.isEmpty()
        || m_displayPipeline.loadGate().containsPendingWorkspacePath(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    item->setSelected(true);
    m_framing.armFit();
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
    m_displayPipeline.scheduleImageModePreferCacheClimb(path, wantEdge);
}


void ImageView::requestEscalateClimb(const QString &path, int wantEdge)
{
    m_displayPipeline.requestEscalateClimb(path, wantEdge);
}


void ImageView::installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                             const QImage &image,
                                             SessionAppearance::PixelKind kind)
{
    m_displayPipeline.installImageModeSampleInPlace(item, path, image, kind);
}

int ImageView::cappedDisplayEdgeForPath(const QString &path, int wantEdge) const
{
    return m_displayPipeline.cappedDisplayEdgeForPath(path, wantEdge);
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
    // Never ImageLoader::load on the GUI thread — that was the crop-enter stall
    // on multi-MP files. Callers use the best available sample (cache / item)
    // and schedule requestCropFullRaster / PathRaster for a native upgrade.
    return cached;
}

bool ImageView::sampleCoversNativeLogical(const QString &path, const QImage &image) const
{
    return m_displayPipeline.sampleCoversNativeLogical(path, image);
}

void ImageView::noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                                 const QImage &sample)
{
    m_displayPipeline.noteImageModePreferCacheDelivery(path, requestEdge, sample);
}


void ImageView::ensureImageModeQualityClimb(const QString &path, const QImage &sample)
{
    m_displayPipeline.ensureImageModeQualityClimb(path, sample);
}


bool ImageView::tryInstallImageModeSample(const QString &path, const QImage &image)
{
    return m_displayPipeline.tryInstallImageModeSample(path, image);
}

bool ImageView::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    return m_displayPipeline.tryInstallImageModeSampleBaked(path, image, kind);
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

void ImageView::scheduleTileLodAfterInteraction(int delayMs)
{
    if (!m_displayPipeline.tileLodZoomDebounce()) {
        m_displayPipeline.tileLodZoomDebounce() = new QTimer(this);
        m_displayPipeline.tileLodZoomDebounce()->setSingleShot(true);
        connect(m_displayPipeline.tileLodZoomDebounce(), &QTimer::timeout, this, [this]() {
            if (isGalleryMode()) {
                tickPrimaryTileLod(8);
                return;
            }
            if (isImageMode()) {
                maybeClimbImageModePixelsForView();
            } else if (isWorkspaceMode()) {
                ensureWorkspaceQualityClimb();
            }
        });
    }
    m_displayPipeline.tileLodZoomDebounce()->setInterval(ViewTransform::nonNegMs(delayMs));
    m_displayPipeline.tileLodZoomDebounce()->start();
}

void ImageView::prefetchTilesForPaths(const QStringList &paths, int budgetPerPath)
{
    m_tileNeighborPrefetch.prefetchPaths(paths, budgetPerPath);
}

bool ImageView::pathOnLiveCanvas(const QString &path) const
{
    for (const ImageItem *ii : m_items) {
        if (ii && ii->path() == path) {
            return true;
        }
    }
    return false;
}

QSize ImageView::viewportWidgetSize() const
{
    if (const QWidget *vp = viewport()) {
        return vp->size();
    }
    return {};
}

qreal ImageView::prefetchDevicePixelRatio() const
{
    if (const QWidget *vp = viewport()) {
        return vp->devicePixelRatioF();
    }
    return 1.0;
}

bool ImageView::tilePrefetchNavHot() const
{
    return m_slideshow.hud().isNavHot();
}


void ImageView::dropTilePrefetchPath(const QString &path)
{
    m_tileNeighborPrefetch.dropPath(path);
}

void ImageView::purgeTilePathRam(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    for (ImageItem *item : m_items) {
        if (item && item->path() == path) {
            item->dropTileLodSession();
        }
    }
    tilelod::TileLodRegistry::instance().invalidate(path);
    dropTilePrefetchPath(path);
}

void ImageView::dropAllTileLodSessions()
{
    auto dropList = [](const QList<ImageItem *> &items) {
        for (ImageItem *item : items) {
            if (item) {
                item->dropTileLodSession();
            }
        }
    };
    dropList(m_items);
    dropList(m_gallery.stashedItems());
    dropList(m_workspace.stashedItems());
}

void ImageView::tickPrimaryTileLod(int budget)
{
    ASSERT_GUI_THREAD();
    // Key-repeat: do not plan/issue tiles — soft underlay only until settle.
    if (m_slideshow.hud().isNavHot()) {
        return;
    }
    if (!m_displayPipeline.tileCoordinator()) {
        m_displayPipeline.tileCoordinator() = std::make_unique<TileLoadCoordinator>(this);
    }
    m_displayPipeline.tileCoordinator()->tick(budget);

    // Image/Workspace: keep issuing until every tileLodWanted item is covered.
    // Without a re-arm, only the first budget of center keys climbed to target
    // scale; outer cells stayed one level coarse until a scroll forced a tick.
    bool needMore = false;
    for (ImageItem *ii : m_items) {
        if (!ii || !ii->tileLodWanted()) {
            continue;
        }
        if (!ii->tileLodViewportCovered()) {
            needMore = true;
            break;
        }
    }
    if (!needMore) {
        return;
    }
    if (!m_displayPipeline.tileLodTimer()) {
        m_displayPipeline.tileLodTimer() = new QTimer(this);
        m_displayPipeline.tileLodTimer()->setSingleShot(true);
        connect(m_displayPipeline.tileLodTimer(), &QTimer::timeout, this, [this]() {
            // Image focus: higher budget so density climb is not starved.
            tickPrimaryTileLod(isGalleryMode() ? 48 : 32);
        });
    }
    if (!m_displayPipeline.tileLodTimer()->isActive()) {
        m_displayPipeline.tileLodTimer()->start(16);
    }
}


void ImageView::maybeClimbImageModePixelsForView()
{
    m_displayPipeline.maybeClimbImageModePixelsForView();
}

void ImageView::completeLoadReplace(const QString &path, const QImage &image, quint64 generation)
{
    m_displayPipeline.completeLoadReplace(path, image, generation);
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Host path→raster map: keep display-ladder samples (≤ kDisplayMaxEdge),
    // not a hard 512 preview. Slideshow must reuse Image-mode sharpness.
    if (!image.isNull() && !path.isEmpty()) {
        ImageCache::put(path, image);
        // Quality/soft climb during slideshow → phase buffer + atlas upgrade.
        if (m_slideshow.hud().isProgressActive()) {
            m_slideshow.onSlideshowRasterReady(path, image);
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
    m_textLayer.clearLinkHoverTip();
    if (m_textLayer.showsRegions() || m_textLayer.hasSearchQuery()) {
        refreshTextLayer();
    }
    m_sessionId.clearLastLoadError();

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

void ImageView::ensureImageFocusSurface()
{
    if (!isImageMode()) {
        if (m_displayPipeline.imageFocusSurfaceRef() != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            m_displayPipeline.imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = primaryItem();
    if (!item || item->path().isEmpty()) {
        m_displayPipeline.imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        return;
    }
    // Prefer the canvas item registry (create/destroy lifecycle).
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    // Kind may have been GalleryTile if item was created before mode switch.
    const auto itemSid =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    const DisplaySurface::Binding *ib = m_displayPipeline.displaySurfaces().binding(itemSid);
    if (ib && ib->kind != DisplaySurface::Kind::ImageFocus) {
        registerItemDisplaySurface(item); // rebind as ImageFocus
    }
    m_displayPipeline.imageFocusSurfaceRef() =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
}

void ImageView::syncImageFocusSurfaceState()
{
    ensureImageFocusSurface();
    if (m_displayPipeline.imageFocusSurfaceRef() == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    ImageItem *item = primaryItem();
    if (!item) {
        return;
    }
    const QString path = item->path();
    const bool pending =
        m_pathRaster && !path.isEmpty() && m_pathRaster->isClimbPending(path);
    syncItemDisplaySurface(item, -1, pending);
    if (m_cropCtrl.session().isDraftSampleFrozen() && isCropDraftLockedPath(path)) {
        m_displayPipeline.displaySurfaces().setFrozen(m_displayPipeline.imageFocusSurfaceRef(), true);
    }
}

void ImageView::driveImageFocusSurface()
{
    m_displayPipeline.driveImageFocusSurface();
}

void ImageView::registerItemDisplaySurface(ImageItem *item)
{
    m_displayPipeline.registerItemDisplaySurface(item);
}

void ImageView::unregisterItemDisplaySurface(ImageItem *item)
{
    m_displayPipeline.unregisterItemDisplaySurface(item);
}

void ImageView::syncItemDisplaySurface(ImageItem *item, int hostLongEdge,
                                       bool climbPending)
{
    m_displayPipeline.syncItemDisplaySurface(item, hostLongEdge, climbPending);
}

