#include <cstdlib>
#include <cstdio>
// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "thumtoocache.h"

#include <QDebug>
#include "imageitem.h"
#include "imageloader.h"
#include "imagecache.h"
#include "archivepath.h"
#include "pagepath.h"
#include "sessionappearance.h"

#include <QFileInfo>
#include <QScrollBar>
#include <QThreadPool>
#include <QTimer>
#include <QDateTime>
#include <QPointer>
#include <QMetaObject>
#include <QtMath>

ImageItem *ImageView::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    auto *item = new ImageItem(path, image);
    applyItemModeFlags(item);
    // Session crop survives navigation: apply only on full on-disk decodes.
    // Workspace Duplicate passes already-final pixels (possibly cropped) — do
    // not re-apply the path crop or the rect is interpreted on the wrong size.
    if (applyStoredSessionCrop) {
        // Prefer stable session-image id appearance; path map is legacy only.
        //
        // Image mode LoadReplace: the sole canvas item is the current session
        // image, so m_currentSessionId / m_sessionIndex identify it correctly.
        //
        // Gallery / Workspace LoadAdd: each tile is bound to its own session id
        // *after* creation (pendingSessionBinds). Using m_currentSessionId here
        // would bake the *navigated* image's crop into every newly decoded tile
        // when leaving Image crop for Gallery — do not apply cursor appearance
        // in multi-item modes.
        const WorkspaceItemState *app = nullptr;
        WorkspaceItemState pathFallback;
        if (isImageMode()) {
            if (m_currentSessionId != kInvalidSessionImageId) {
                seedSessionAppearanceFromState(m_currentSessionId, path);
                if (const WorkspaceItemState *sit = m_appearance.get(m_currentSessionId)) {
                    app = &(*sit);
                }
                // Bound session image with no appearance entry = full frame, no path fallback.
            } else {
                // Path map only when unbound (no session image id).
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    pathFallback = *it;
                    app = &pathFallback;
                }
            }
        }
        if (app) {
            SessionAppearance::applyContentToItem(item, *app);
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

void ImageView::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    if (!item || pixels.isNull()) {
        return;
    }
    const QSize layoutBefore = item->imageSize();

    // Resolve session id before seed (Image-mode soft path often passes invalid sid).
    if (sid == kInvalidSessionImageId) {
        if (item->sessionId() != kInvalidSessionImageId) {
            sid = item->sessionId();
        } else if (isImageMode() && m_currentSessionId != kInvalidSessionImageId) {
            sid = m_currentSessionId;
        }
    }
    seedSessionAppearanceFromState(sid, item->path());

    const WorkspaceItemState *app = nullptr;
    WorkspaceItemState pathFallback;
    if (sid != kInvalidSessionImageId) {
        app = m_appearance.get(sid);
    } else if (sid == kInvalidSessionImageId && item->sessionId() == kInvalidSessionImageId) {
        // Unbound tile only: path map is legacy fallback (IDENTITY.md).
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            pathFallback = *it;
            app = &pathFallback;
        }
    }

    // Single pipeline: raw pixels → materializeDisplay → attach. Never bake
    // twice. Gallery soft and Image full use the same function + session state.
    WorkspaceItemState appearance;
    if (app) {
        appearance = *app;
    }
    // Gallery soft paint budget: do not attach multi-megapixel soft when the
    // on-screen cell only needs ~256–512. Full soft stays in ImageCache for
    // zoom-in; soft.have is tracked separately so we do not re-climb.
    QImage pixelsForDisplay = pixels;
    if (isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        const int have = qMax(pixels.width(), pixels.height());
        const int need = galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
        if (need > 0 && have > need * 2) {
            const int target =
                qMax(need, ThumtooCache::kFilmstripLadderEdge);
            if (have > target) {
                pixelsForDisplay = pixels.scaled(
                    target, target, Qt::KeepAspectRatio,
                    Qt::FastTransformation);
            }
        }
    }
    const QImage display =
        SessionAppearance::materializeDisplay(pixelsForDisplay, appearance, kind);

    if (kind == SessionAppearance::PixelKind::FullSource) {
        item->setSourceImage(display);
        // Full pixels are stable — cache the painted tile again.
        if (item->cacheMode() != QGraphicsItem::DeviceCoordinateCache) {
            item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
        } else {
            item->invalidateDeviceCache();
        }
    } else {
        item->setPreviewImage(display); // NoCache soft path
    }
    item->setContentHFlip(appearance.contentHFlip);
    item->setContentVFlip(appearance.contentVFlip);
    item->setSessionCrop(appearance.hasCrop, appearance.cropRect);
    item->setColorAdjustments(appearance.colorAdjust);
    SessionAppearance::syncItemLayoutToContentOrientation(item, appearance);

    // Do NOT emit sessionAppearanceChanged from decode/install.
    // Soft ladder upgrades (including after Gallery focus) must not rewrite the
    // filmstrip — that is selection-coupled and wrong. Filmstrip overrides are
    // emitted only from user content edits (commit / bake / crop accept).

    // Gallery reflow only when layout geometry actually changed — soft ladder
    // upgrades on focus must not repack the whole grid (click should not move tiles).
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

void ImageView::installImageModePendingTile(const QString &path, const QImage &preview)
{
    if (!isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the viewport with dwell/live blits. Pending tile used to
    // clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed (logs: underlayVisible=true item="-",
    // dwellT never restarted). Underlay is hidden for the whole show.
    if (m_slideshowProgressActive
        || m_liveTransitionActive || m_liveTransitionHold || m_liveTransitionAwaitingLoad) {
        return;
    }
    // Prefer explicit preview, session map, shared ImageCache (filmstrip/gallery
    // soft), then slideshow full map — so next/prev paints immediately.
    QImage pixels = preview;
    if (pixels.isNull()) {
        const auto it = m_previewByPath.constFind(path);
        if (it != m_previewByPath.cend()) {
            pixels = it.value();
        }
    }
    if (pixels.isNull()) {
        pixels = ImageCache::get(path);
    }
    if (pixels.isNull()) {
        const auto fit = m_ssFullByPath.constFind(path);
        if (fit != m_ssFullByPath.cend() && !fit->isNull()) {
            pixels = *fit;
        }
    }
    // Layout size = native when known; else preview aspect so fitInView fills
    // the window (not a provisional square that letterboxes the content).
    const QSize sz = layoutSizeForPath(path, pixels);

    setUpdatesEnabled(false);
    // Wipe the underlay under an active dwell camera would leave motion pointing
    // at a destroyed item — drop motion first; full load restarts it.
    if (m_slideshowProgressActive) {
        // User next/prev (no live hold): drop transition leftovers and per-image
        // motion biases so the new dwell / next auto-transition starts clean.
        // Mid live-advance keeps composite state; only cancel underlay motion.
        if (!(m_liveTransitionActive || m_liveTransitionHold
              || m_liveTransitionAwaitingLoad)) {
            cancelSlideshowTransition();
            m_motionBiasValid = false;
            m_motionBiasPath.clear();
        }
        cancelSlideshowMotion();
    }
    clearLiveCanvas();
    ImageItem *item = createPlaceholderItem(path, sz);
    if (!item) {
        setUpdatesEnabled(true);
        return;
    }
    if (m_currentSessionId != kInvalidSessionImageId) {
        item->setSessionId(m_currentSessionId);
    }
    if (m_sessionIndex >= 0) {
        item->setSessionIndex(m_sessionIndex);
    }
    if (!pixels.isNull()) {
        installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                             m_currentSessionId);
    }
    item->setInteractive(false);
    item->setScaleHandlesEnabled(false);
    item->setItemScale(1.0);
    item->setPos(0, 0);
    item->setItemRotation(0.0);
    prepareImageModeCanvas();
    if (m_slideshowProgressActive) {
        // Stay on slideshow zoom (Fit/Fill/Actual), not normal Image fit.
        applySlideshowZoomFraming(item);
    } else {
        fitItem(item, currentFitAspectMode());
    }
    m_scene->setSceneRect(item->sceneBoundingRect().adjusted(-8, -8, 8, 8));
    setUpdatesEnabled(true);
    if (viewport()) {
        viewport()->update();
    }
    // Avoid statusChanged here during slideshow — updateNavigationActions must
    // not run mid-nav (and must not treat a transient empty canvas as end-of-show).
    if (!m_slideshowProgressActive) {
        emit statusChanged();
    }
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
    // (Cannot use ?: on atomic — pre-increment yields T, bare atomic is not T.)
    quint64 gen = m_loadGeneration.load();
    if (role == LoadReplace) {
        gen = ++m_loadGeneration;
        // FocusFull: tell thumtoo this is Primary (overview + tile pyramid).
        if (isImageMode()) {
            (void)ThumtooCache::setPrimaryInterest(
                path, ThumtooCache::kBatchOverviewEdge);
        }
    }
    emit statusChanged(); // pending count for status bar

    // Slideshow dual-blit already decoded this path (handoff) or preload did —
    // reuse pixels so goNext does not pay a second disk decode under the hold.
    if (role == LoadReplace) {
        QImage ready;
        if (path == m_handoffPath && !m_handoffImage.isNull()) {
            ready = m_handoffImage;
            m_handoffPath.clear();
            m_handoffImage = QImage();
        } else if (path == m_preloadPath && !m_preloadImage.isNull()) {
            ready = m_preloadImage;
            m_preloadPath.clear();
            m_preloadImage = QImage();
        } else if (m_ssFullByPath.contains(path)
                   && !m_ssFullByPath.value(path).isNull()) {
            ready = m_ssFullByPath.value(path);
        }
        if (!ready.isNull()) {
            const QPointer<ImageView> guard(this);
            QMetaObject::invokeMethod(guard, "onImageLoaded", Qt::QueuedConnection,
                                      Q_ARG(QString, path),
                                      Q_ARG(QImage, ready),
                                      Q_ARG(quint64, gen),
                                      Q_ARG(int, static_cast<int>(role)));
            return;
        }
    }

    // Image mode: drop the previous frame immediately so rapid next/prev is not
    // stuck on the old image until the background decode finishes. Reuses a
    // cached preview for this path when available.
    if (role == LoadReplace && isImageMode()) {
        installImageModePendingTile(path);
    }

    // QPointer: worker must not touch a destroyed view. Generation filters
    // superseding once the slot runs on the GUI thread.
    const QPointer<ImageView> guard(this);
    constexpr int kPreviewEdge = 512;

    // Thumbnail (high priority) and full decode (low priority) in parallel so
    // rapid next/prev paints soft pixels first; full frames catch up in the background.
    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        const QImage preview = ImageLoader::loadThumbnail(path, kPreviewEdge);
        if (!guard || preview.isNull()) {
            return;
        }
        // QTimer::singleShot(context, functor) is safer than
        // QMetaObject::invokeMethod(context, functor) under Qt 6.11 — the latter
        // asserted "Called object is not of the correct type" during drop loads.
        QTimer::singleShot(0, guard.data(), [guard, path, preview, gen, role]() {
            if (!guard) {
                return;
            }
            guard->onImagePreviewLoaded(path, preview, gen, static_cast<int>(role));
        });
    }, 2);

    // Full decode at low priority so soft previews win the pool under rapid nav.
    QThreadPool::globalInstance()->start([guard, path, role, gen]() {
        // Superseded navigation: skip expensive full decode when possible.
        if (!guard || gen != guard->m_loadGeneration.load()) {
            return;
        }
        const QImage image = ImageLoader::load(path);
        if (!guard) {
            return;
        }
        QTimer::singleShot(0, guard.data(), [guard, path, image, gen, role]() {
            if (!guard) {
                return;
            }
            guard->onImageLoaded(path, image, gen, static_cast<int>(role));
        });
    }, -1);
}


int ImageView::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    // On-screen long edge in device pixels, snapped to a ladder step.
    // Visible tiles request that edge (no soft-max cliff, no native full dump).
    // Off-screen / idle placeholders stay in the soft band to limit work.
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

void ImageView::scheduleGalleryDecode(const QString &path)
{
    if (!isGalleryMode() || path.isEmpty()) {
        return;
    }
    // Size-first still probes in the background, but never blocks decode:
    // provisional 1000×1000 or LQIP-only layout must still upgrade to the
    // zoom-appropriate ladder edge (display-sized, not native full).
    if (isProvisionalImageSize(path)) {
        scheduleImageSizeProbe(path);
    }
    GallerySoftState &st = m_gallerySoft[path];
    if (st.failed) {
        return;
    }
    // Climb / SoftOnly request only when not already waiting on a callback.
    if (st.inflight > 0 || st.fullInflight) {
        return;
    }

    // Fast path: updateGalleryDecodeWindow already filled st.have / st.want.
    // Do not re-scan all items or re-host soft (pass1 owns host installs) —
    // that was O(n²) and multi-hundred-ms on ~100-tile galleries.
    int have = st.have;
    int want = st.want;
    if (want <= 0) {
        const QRect viewRect = viewport()->rect().adjusted(
            -kGalleryDecodeOverscanPx, -kGalleryDecodeOverscanPx,
            kGalleryDecodeOverscanPx, kGalleryDecodeOverscanPx);
        const QRectF sceneVisible = mapToScene(viewRect).boundingRect();

        have = 0;
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
        st.have = have;
        if (anyFull) {
            return;
        }

        // Host soft only for direct callers (not decode-window pass2).
        if (have <= 0) {
            QImage hostSoft = ImageCache::get(path);
            if (hostSoft.isNull()) {
                hostSoft = m_previewByPath.value(path);
            }
            if (!hostSoft.isNull()) {
                onImagePreviewLoaded(path, hostSoft, m_loadGeneration.load(),
                                     static_cast<int>(LoadAdd));
                have = qMax(have, qMax(hostSoft.width(), hostSoft.height()));
                st.have = have;
            }
        }

        want = galleryWantEdgeForPath(path, sceneVisible);
        st.want = want;
    }
    if (want <= 0 || have >= want) {
        return;
    }

    // Higher zoom/need than a prior soft shortfall — retry the new edge.
    if (st.gaveUpWant > 0 && want > st.gaveUpWant) {
        st.gaveUpWant = 0;
    }
    if (st.gaveUpWant >= want && have > 0) {
        return;
    }
    if (gallerySoftInflightCount() >= galleryDecodeConcurrency()) {
        return;
    }

    // Progressive soft: PreferCache / SoftOnly only up to kGalleryLadderEdge
    // (512). Requesting 1024 here installed multi-megapixel softs on every tile
    // via the pool callback and stalled the GUI (need=1024 logs). Overview at
    // 1024+ is setInterest's job, not host soft install.
    const int softCap = ThumtooCache::kGalleryLadderEdge;
    const int target = qMin(want, softCap);
    // Already have the durable soft max — stop PreferCache spin.
    if (have >= target * 9 / 10) {
        if (want > softCap) {
            st.gaveUpWant = qMax(st.gaveUpWant, want);
        }
        return;
    }
    // Intermediate = previous ladder step under target (1024→512, 512→256, …).
    const int intermediate = ThumtooCache::prevLadderEdge(target);
    int requestEdge = target;
    if (intermediate > 0 && have < intermediate * 9 / 10) {
        requestEdge = intermediate;
    }

    st.inflight = requestEdge;
    st.inflightSinceMs = QDateTime::currentMSecsSinceEpoch();
    addPendingWorkspacePath(path);
    // Progressive: keep climbing while have < want (soft → overview).
    // Shortfall settle lives in ThumtooCache::g_pixelsSettled / gaveUpWant so
    // we do not spin the same SoftOnly edge forever when only Embedded exists.
    emit statusChanged();

    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/gallery: soft request path need=%d have=%d req=%d "
                "inter=%d gaveUp=%d\n",
                want, have, requestEdge, intermediate, st.gaveUpWant);
    }

    const quint64 gen = m_loadGeneration.load();
    const QPointer<ImageView> guard(this);
    QThreadPool::globalInstance()->start([guard, path, gen, requestEdge]() {
        // Soft only — loadThumbnail may schedulePixels once on miss; we never
        // clear settle / re-queue the same edge in a loop.
        const QImage preview = ImageLoader::loadThumbnail(path, requestEdge);
        if (!guard) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, path, preview, gen, requestEdge]() {
                ImageView *const host = guard.data();
                if (!host) {
                    return;
                }
                auto it = host->m_gallerySoft.find(path);
                if (it == host->m_gallerySoft.end()) {
                    host->takePendingWorkspacePath(path);
                    return;
                }
                GallerySoftState &soft = it.value();
                // Superseded (ladderReady cleared inflight, or a newer edge was
                // requested): still install any soft we decoded — dropping it
                // left blank tiles when SoftOnly finished before this callback.
                if (soft.inflight != requestEdge) {
                    host->takePendingWorkspacePath(path);
                    if (!preview.isNull()) {
                        const int got = qMax(preview.width(), preview.height());
                        host->onImagePreviewLoaded(
                            path, preview, gen,
                            static_cast<int>(LoadAdd));
                        soft.have = qMax(soft.have, got);
                        if (const char *dbg = std::getenv("THUMTOO_DEBUG");
                            dbg && dbg[0] && dbg[0] != '0') {
                            fprintf(stderr,
                                    "biltoo/gallery: INSTALL soft path=%s got=%d "
                                    "(superseded pool callback)\n",
                                    qPrintable(QFileInfo(path).fileName()), got);
                        }
                    }
                    if (host->isGalleryMode()) {
                        host->scheduleGalleryDecodeWindowRefresh(48);
                    }
                    return;
                }
                host->takePendingWorkspacePath(path);

                // Install any soft we got. Climb only when below the request
                // edge — and only keep inflight when a host callback is truly
                // pending (schedule* returned true). SKIP (already settled /
                // inflight elsewhere) must not pin soft.inflight forever: that
                // blocked scheduleGalleryDecode while tiles stayed blank even
                // though PreferCache already had filmstrip soft in the DB.
                int got = preview.isNull()
                              ? 0
                              : qMax(preview.width(), preview.height());
                if (!preview.isNull()) {
                    host->onImagePreviewLoaded(path, preview, gen,
                                               static_cast<int>(LoadAdd));
                    soft.have = qMax(soft.have, got);
                    if (const char *dbg = std::getenv("THUMTOO_DEBUG");
                        dbg && dbg[0] && dbg[0] != '0') {
                        fprintf(stderr,
                                "biltoo/gallery: INSTALL soft path=%s got=%d "
                                "request=%d (pool PreferCache)\n",
                                qPrintable(QFileInfo(path).fileName()), got,
                                requestEdge);
                    }
                }
                auto clearInflight = [&]() {
                    soft.inflight = 0;
                    soft.inflightSinceMs = 0;
                };
                if (got >= requestEdge * 9 / 10) {
                    clearInflight();
                    if (soft.gaveUpWant <= requestEdge) {
                        soft.gaveUpWant = 0;
                    }
                } else if (ThumtooCache::isAvailable()
                           && requestEdge <= ThumtooCache::kGalleryLadderEdge) {
                    const bool queued =
                        ThumtooCache::schedulePixels(path, requestEdge);
                    if (queued) {
                        soft.inflight = requestEdge;
                        soft.inflightSinceMs =
                            QDateTime::currentMSecsSinceEpoch();
                    } else {
                        // Settled or already building: PreferCache may now have
                        // soft (filmstrip SoftOnly finished without host cb).
                        const QImage again =
                            ImageLoader::loadThumbnail(path, requestEdge);
                        const int againGot = again.isNull()
                            ? 0
                            : qMax(again.width(), again.height());
                        if (!again.isNull() && againGot > soft.have) {
                            host->onImagePreviewLoaded(
                                path, again, gen,
                                static_cast<int>(LoadAdd));
                            soft.have = qMax(soft.have, againGot);
                            if (const char *dbg = std::getenv("THUMTOO_DEBUG");
                                dbg && dbg[0] && dbg[0] != '0') {
                                fprintf(stderr,
                                        "biltoo/gallery: INSTALL soft path=%s "
                                        "got=%d after SKIP schedulePixels\n",
                                        qPrintable(QFileInfo(path).fileName()),
                                        againGot);
                            }
                        }
                        clearInflight();
                        if (againGot < requestEdge * 9 / 10 && soft.have > 0) {
                            soft.gaveUpWant =
                                qMax(soft.gaveUpWant, requestEdge);
                        }
                    }
                } else if (ThumtooCache::isAvailable()
                           && requestEdge > ThumtooCache::kGalleryLadderEdge
                           && requestEdge <= ThumtooCache::kBatchOverviewEdge) {
                    const int ov =
                        qMin(requestEdge, ThumtooCache::kBatchOverviewEdge);
                    const bool queued =
                        ThumtooCache::scheduleOverviewPixels(path, ov);
                    if (queued) {
                        soft.inflight = ov;
                        soft.inflightSinceMs =
                            QDateTime::currentMSecsSinceEpoch();
                    } else {
                        clearInflight();
                    }
                } else if (ThumtooCache::isAvailable()
                           && requestEdge > ThumtooCache::kBatchOverviewEdge) {
                    clearInflight();
                    soft.gaveUpWant = qMax(soft.gaveUpWant, requestEdge);
                } else {
                    clearInflight();
                    soft.gaveUpWant = qMax(soft.gaveUpWant, requestEdge);
                    // Do not set failed=true on first miss — PreferCache / SoftOnly
                    // may still deliver; failed is permanent and skips forever.
                }

                // Coalesce status + decode-window — every INSTALL used to
                // refresh the HUD and rescan the gallery on the GUI thread.
                host->scheduleGalleryDecodeWindowRefresh(48);
                host->refreshStatus();
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
    // Session cache for rapid revisit (path is the decode source; size is small).
    m_previewByPath.insert(path, image);
    // Replace navigations: drop superseded previews.
    if (role == LoadReplace && generation != m_loadGeneration.load()) {
        return;
    }
    if (role == LoadReplace) {
        if (path != classicPath()) {
            return;
        }
        if (isImageMode()) {
            if (ImageItem *cur = targetItem()) {
                if (cur->path() == path && cur->hasDecodedPixels()) {
                    return; // full decode already won the race
                }
                // Upgrade loading placeholder (same path) in place when possible.
                if (cur->path() == path && !cur->hasDecodedPixels()) {
                    // Soft install gate bakes appearance and syncs layout aspect.
                    // Do not setIntrinsicSize from unoriented preview pixels first.
                    {
                        const SessionImageId sid =
                            cur->sessionId() != kInvalidSessionImageId
                                ? cur->sessionId()
                                : m_currentSessionId;
                        installDisplayPixels(cur, image,
                                             SessionAppearance::PixelKind::SoftPreview,
                                             sid);
                    }
                    // Always re-frame: provisional size adopt and/or content
                    // orientation swap can change the fit box.
                    if (!m_slideshowProgressActive) {
                        fitItem(cur, currentFitAspectMode());
                    } else {
                        applySlideshowZoomFraming(cur);
                    }
                    if (m_scene) {
                        m_scene->setSceneRect(
                            cur->sceneBoundingRect().adjusted(-8, -8, 8, 8));
                    }
                    if (viewport()) {
                        viewport()->update();
                    }
                    return;
                }
            }
            installImageModePendingTile(path, image);
            return;
        }
        // Empty multi-item canvas: fall through to per-item fill.
    }
    // Gallery / Workspace: fill undecoded occurrences of this path.
    // If layout size was still provisional, adopt preview aspect and re-pack
    // so low-res tiles occupy the same footprint as the eventual full image
    // (otherwise pack stays on 1000×1000 and the preview looks unstretched).
    bool gallerySizeChanged = false;
    for (ImageItem *item : m_items) {
        if (!item || item->path() != path || item->hasDecodedPixels()) {
            continue;
        }
        // Keep a sharper preview; do not replace with a smaller ladder step.
        const int incoming = qMax(image.width(), image.height());
        if (item->hasDisplayPixels() && item->displayPixelLongEdge() >= incoming) {
            continue;
        }
        // Do not adopt unoriented soft dimensions as layout. installDisplayPixels
        // bakes content appearance and syncs intrinsic aspect; a pre-set raw
        // soft size caused oversized/wrong AABB when focus upgraded the ladder.
        const QSize before = item->imageSize();
        {
            const SessionImageId sid = item->sessionId();
            installDisplayPixels(item, image,
                                 SessionAppearance::PixelKind::SoftPreview, sid);
        }
        if (item->imageSize() != before) {
            gallerySizeChanged = true;
        }
        // Mark dirty; one coalesced viewport update below (not per-tile scene
        // update storms when many ladderReady events land together).
        item->update();
    }
    if (gallerySizeChanged && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (viewport()) {
        // Coalesce: single full viewport refresh after this batch of installs.
        viewport()->update();
    }
}

void ImageView::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Feed shared cache (preview-sized) so slideshow/gallery reuse this decode.
    if (!image.isNull() && !path.isEmpty()) {
        const int edge = ImageCache::kPreviewEdge;
        if (qMax(image.width(), image.height()) > edge) {
            ImageCache::put(path, image.scaled(edge, edge, Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
        } else {
            ImageCache::put(path, image);
        }
    }
    // Replace loads only care about the latest request
    if (role == LoadReplace) {
        if (generation != m_loadGeneration) {
            return; // superseded by a newer navigation / open
        }
        if (path != classicPath() || isMultiItemMode()) {
            // Stale image-mode navigation or switched to workspace
            if (!(isMultiItemMode() && m_items.isEmpty() && path == classicPath())) {
                if (path != classicPath()) {
                    return;
                }
            }
        }
        if (image.isNull()) {
            if (ThumtooCache::isAvailable()
                && (PagePath::isPdfImageRef(path) || PagePath::isPageRef(path))) {
                // Soft miss: schedule ladder; status bar keeps loading until
                // ladderReady / a later successful load.
                ThumtooCache::scheduleProbe(path);
                ThumtooCache::schedulePixels(
                    path, qMax(ThumtooCache::kGalleryLadderEdge, 512));
                m_lastLoadError.clear();
            } else {
                m_lastLoadError = path;
            }
            emit statusChanged();
            return;
        }
        if (isImageMode()) {
            m_lastLoadError.clear();
            rememberImageSize(path, image.size());
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
            // Bind to the session cursor so Image-mode crop/flip targets the
            // matching Workspace slot (not every canvas instance of this path).
            if (m_currentSessionId != kInvalidSessionImageId) {
                item->setSessionId(m_currentSessionId);
            }
            if (m_sessionIndex >= 0) {
                item->setSessionIndex(m_sessionIndex);
            }
            // Filmstrip overrides are not driven by decode (selection/nav).
            // Never inherit Gallery/Workspace placement or scale.
            // DOMAIN: flips/crop and *cardinal* rotation persist across navigation.
            // Arbitrary Workspace rotation stays on the free-form item only.
            // Crop was applied in createItemFromImage from m_itemStates.
            item->setInteractive(false);
            item->setScaleHandlesEnabled(false);
            item->setItemScale(1.0);
            item->setPos(0, 0);
            {
                // Image mode: no Workspace placement rotation. Content 90°/flip
                // are already in pixels (createItemFromImage applies content bakes).
                item->setItemRotation(0.0);
                const auto it = m_itemStates.constFind(path);
                if (it != m_itemStates.cend()) {
                    // Legacy unbaked flips only if content flags not used yet.
                    if (!it->contentHFlip && !it->contentVFlip) {
                        item->setItemHFlip(it->hFlip);
                        item->setItemVFlip(it->vFlip);
                    }
                }
            }
            prepareImageModeCanvas();
            // Slideshow framing: when dwell motion is on, the camera sets the
            // transform (including handoff from a live transition). Applying
            // zoom framing first would centre the image then jump to motion t0.
            if (m_slideshowProgressActive
                && m_slideshowMotion == SlideshowMotion::Off) {
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
                // Paused ←/→ loads the underlay while pure phase still paints
                // the previous path — refresh dwell to this decode.
                setSlideshowPhase(path, QString(), -1.0);
            }
            // Drop held live-transition overlay only after the new item is fitted
            // (and motion sample applied) so the outgoing underlay never flashes.
            releaseLiveTransitionHold();
            setUpdatesEnabled(true);
            if (m_slideshowTransitionPending) {
                startSlideshowTransitionAnimation();
            } else {
                viewport()->update();
            }
            emit statusChanged();
            return;
        }
        // Workspace with empty canvas: seed with navigated image — only for
        // genuine session navigation. Project load / membership adds schedule
        // LoadAdd with pending binds; seeding first would leave an unbound tile
        // (default placement, no flip/grade) and steal the first path's LoadAdd.
        if (m_items.isEmpty()
            && m_pendingSessionBinds.isEmpty()
            && !m_pendingWorkspacePaths.contains(path)) {
            ImageItem *item = createItemFromImage(path, image);
            if (item) {
                item->setSelected(true);
                m_fitMode = true;
                fitItem(item, currentFitAspectMode());
                emit statusChanged();
            }
        }
        return;
    }

    // Workspace add / restore
    if (role == LoadRestore) {
        // AUDIT M27: claim one pending restore state for this path (duplicates OK).
        int claim = -1;
        for (int i = 0; i < m_pendingRestoreStates.size(); ++i) {
            if (m_pendingRestoreStates.at(i).path == path) {
                claim = i;
                break;
            }
        }
        if (claim < 0) {
            return;
        }
        const WorkspaceItemState state = m_pendingRestoreStates.takeAt(claim);
        if (image.isNull()) {
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
        return;
    }

    // LoadAdd: workspace new item, or Gallery placeholder fill / virtual window.
    // Duplicate paths are separate session images: fill every undecoded live
    // occurrence, then create until live count matches pathOrder occurrences.
    gallerySoftResetPath(path);
    // Mode leave / empty Workspace bumps generation and clears pending paths.
    // Reject superseded gallery window decodes so they cannot spawn tiles on
    // Workspace after the user switched modes mid-decode.
    if (generation != m_loadGeneration.load()) {
        emit statusChanged();
        return;
    }
    if (!image.isNull()) {
        rememberImageSize(path, image.size());
    }
    if (!m_pendingWorkspacePaths.contains(path)) {
        // Cancelled (e.g. path removed from session) — drop the result.
        emit statusChanged();
        if (isGalleryMode()) {
            scheduleGalleryDecodeWindowRefresh(48);
        }
        return;
    }
    takePendingWorkspacePath(path);

    if (image.isNull()) {
        // //pdfimage: / //page: with thumtoo: empty sync load is expected while the
        // ladder builds — await ladderReady instead of permanent failure.
        if (ThumtooCache::isAvailable()
            && (PagePath::isPdfImageRef(path) || PagePath::isPageRef(path))) {
            ThumtooCache::scheduleProbe(path);
            // Soft state machine will request placeholder / higher steps.
            m_lastLoadError.clear();
            emit statusChanged();
            if (isGalleryMode()) {
                scheduleGalleryDecodeWindowRefresh(48);
            }
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
        emit statusChanged();
        if (isGalleryMode()) {
            scheduleGalleryDecodeWindowRefresh(48);
        }
        return;
    }

    if (isImageMode()) {
        // Fill stashed Gallery placeholders while user is in Image mode.
        for (ImageItem *cand : m_gallery.stashedItems()) {
            if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
                installDisplayPixels(cand, image,
                                     SessionAppearance::PixelKind::FullSource,
                                     cand->sessionId());
            }
        }
        emit statusChanged();
        return;
    }

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

    int pathOrderCount = 0;
    for (const QString &p : m_pathOrder) {
        if (p == path) {
            ++pathOrderCount;
        }
    }

    // Pending binds whose SessionImageId is already on a live tile are satisfied
    // (placeholders / prior LoadAdd). Drop them so we do not create extras.
    for (int bi = m_pendingSessionBinds.size() - 1; bi >= 0; --bi) {
        const PendingSessionBind &b = m_pendingSessionBinds.at(bi);
        if (b.path != path || b.id == kInvalidSessionImageId) {
            continue;
        }
        if (findItemBySessionId(b.id)) {
            m_pendingSessionBinds.removeAt(bi);
        }
    }

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
        bool haveBound = false;
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            if (m_pendingSessionBinds.at(bi).path != path) {
                continue;
            }
            bound = m_pendingSessionBinds.takeAt(bi);
            haveBound = true;
            break;
        }
        if (!haveBound) {
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
        if (bound.hasScenePos) {
            existing->setGalleryCellSize({});
            existing->setPos(bound.scenePos);
            existing->setItemScale(1.0);
            existing->setItemRotation(0.0);
            existing->setItemShear(0.0);
            existing->setItemOpacity(1.0);
            existing->setStackZ(m_items.size() - 1);
            if (isWorkspaceMode()) {
                existing->setInteractive(true);
                existing->setScaleHandlesEnabled(true);
            }
            m_pendingScenePos.remove(path);
            rememberItemState(existing);
        }
        if (bound.id != kInvalidSessionImageId) {
            // Decode must not rewrite filmstrip (sessionAppearanceChanged).
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                existing->setSelected(true);
            }
        }
    }

    int pendingBinds = 0;
    for (const PendingSessionBind &b : m_pendingSessionBinds) {
        if (b.path == path) {
            ++pendingBinds;
        }
    }

    bool sizeChanged = false;
    int have = 0;
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        if (!existing->hasDecodedPixels()) {
            const QSize before = existing->imageSize();
            const qreal sx0 = existing->itemScaleX();
            const qreal sy0 = existing->itemScaleY() > 0.0 ? existing->itemScaleY()
                                                           : sx0;
            const qreal footW = before.width() * sx0;
            const qreal footH = before.height() * sy0;
            installDisplayPixels(existing, image,
                                 SessionAppearance::PixelKind::FullSource,
                                 existing->sessionId());
            const QSize after = existing->imageSize();
            // Keep scene footprint stable when intrinsic grows (placeholder →
            // full). Workspace is pixel-scaled: if the placeholder already had
            // the native size, after==before and scale stays 1.
            if (isWorkspaceMode() && m_layoutMode == LayoutMode::FreeForm
                && before.isValid() && after.isValid()
                && after.width() > 0 && after.height() > 0
                && (before.width() != after.width()
                    || before.height() != after.height())) {
                existing->setItemScale(footW / qreal(after.width()),
                                       footH / qreal(after.height()));
                sizeChanged = true;
            } else if (before.isValid() && after.isValid()
                       && (before.width() != after.width()
                           || before.height() != after.height())) {
                sizeChanged = true;
            } else {
                existing->update();
            }
        }
    }
    for (ImageItem *cand : m_gallery.stashedItems()) {
        if (cand && cand->path() == path && !cand->hasDecodedPixels()) {
            installDisplayPixels(cand, image,
                                 SessionAppearance::PixelKind::FullSource,
                                 cand->sessionId());
        }
    }

    // Session pathOrder is the multiplicity source of truth. Do not create more
    // tiles than session rows for this path (pending binds only fill gaps).
    int wanted = pathOrderCount;
    if (wanted <= 0) {
        // Not in session pathOrder (ad-hoc workspace place): one tile per bind.
        wanted = qMax(have, pendingBinds > 0 ? pendingBinds : 1);
    }

    // Create missing occurrences (each duplicate is a normal separate tile).
    while (have < wanted) {

        ImageItem *item = createItemFromImage(path, image);
        if (!item) {
            break;
        }
        ++have;
        // Bind pending session row if any remain for this path (FIFO).
        // Skip binds already owned by another live item (should be rare after purge).
        PendingSessionBind bound;
        bool haveBound = false;
        for (int bi = 0; bi < m_pendingSessionBinds.size(); ++bi) {
            if (m_pendingSessionBinds.at(bi).path != path) {
                continue;
            }
            const PendingSessionBind candidate = m_pendingSessionBinds.at(bi);
            if (candidate.id != kInvalidSessionImageId) {
                if (ImageItem *owner = findItemBySessionId(candidate.id)) {
                    if (owner != item) {
                        m_pendingSessionBinds.removeAt(bi);
                        --bi;
                        continue;
                    }
                }
            }
            bound = m_pendingSessionBinds.takeAt(bi);
            haveBound = true;
            if (bound.id != kInvalidSessionImageId) {
                item->setSessionId(bound.id);
            }
            if (bound.index >= 0 && bound.id != kInvalidSessionImageId) {
                item->setSessionIndex(bound.index);
            }
            break;
        }
        applyStoredAppearance(item);
        // Decode/membership must not rewrite filmstrip; user edits emit overrides.
        if (haveBound && bound.id != kInvalidSessionImageId) {
            // Paste: select tiles as they finish decoding.
            if (m_pendingSelectSessionIds.remove(bound.id)) {
                item->setSelected(true);
            }
        }
        if (isGalleryMode()) {
            item->setItemRotation(0.0);
            item->setItemShear(0.0);
            item->setItemHFlip(false);
            item->setItemVFlip(false);
            item->setItemOpacity(1.0);
        } else if (haveBound && bound.hasScenePos) {
            // Explicit drop: place at the drop point (new placement).
            item->setGalleryCellSize({});
            item->setPos(bound.scenePos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemShear(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
            if (isWorkspaceMode()) {
                item->setInteractive(true);
                item->setScaleHandlesEnabled(true);
            }
            m_pendingScenePos.remove(path);
            rememberItemState(item);
        } else if (haveBound && bound.id != kInvalidSessionImageId
                   && m_appearance.get(bound.id)) {
            // Thumbnail membership toggle: restore last Workspace pose for this
            // session image (detach saved it via rememberItemState).
            applyState(item, *m_appearance.get(bound.id));
        } else if (m_pendingScenePos.contains(path)) {
            const QPointF pos = m_pendingScenePos.take(path);
            item->setPos(pos);
            item->setItemScale(1.0);
            item->setItemRotation(0.0);
            item->setItemOpacity(1.0);
            item->setStackZ(m_items.size() - 1);
        } else {
            const auto it = m_itemStates.constFind(path);
            if (it != m_itemStates.cend()) {
                applyState(item, *it);
            } else {
                WorkspaceItemState s = defaultStateForPath(path, m_items.size() - 1);
                const QSizeF sz(image.width(), image.height());
                s.pos = findEmptyPlacement(sz);
                applyState(item, s);
            }
        }
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
    emit statusChanged();
    emit workspacePathsChanged();
    if (isGalleryMode()) {
        scheduleGalleryDecodeWindowRefresh(48);
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

    // Classic mode: decode off the GUI thread. scheduleImageLoad installs a
    // loading tile immediately, then upgrades to thumbnail and full decode.
    scheduleImageLoad(path, LoadReplace);
    emit statusChanged();
    return true;
}
