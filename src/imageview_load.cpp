// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
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
// prepareImageModeDisplaySample removed: workers must return host-raw only.
// GUI installDisplayPixels is the sole materialize site (see CONTENT_PIPELINE
// install invariant). Baking in the worker + put/materialize on the GUI
// double-applied crop and polluted ImageCache with content-baked samples.

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
            // Host-raw only. GUI installDisplayPixels materializes store want
            // once (never bake here — that double-applied crop and polluted
            // ImageCache when the GUI put the baked sample back under path).
            QImage preview = loadSoftPreviewPixels(path, softEdge);
            biltooLoadDbg("softJob DONE path=%s got=%dx%d",
                          qPrintable(QFileInfo(path).fileName()),
                          preview.width(), preview.height());
            if (!preview.isNull() && !path.isEmpty()) {
                ImageCache::put(path, preview);
            }
            Q_UNUSED(sessionApp);
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
            // Host-raw only; GUI install materializes (soft stand-in + async
            // for multi-MP). Do not bake here — same double-bake/cache pollution
            // as the soft job.
            if (!image.isNull() && !path.isEmpty()) {
                ImageCache::put(path, image);
            }
            if (ImageCache::longEdge(image) > qualityEdge) {
                image = image.scaled(qualityEdge, qualityEdge, Qt::KeepAspectRatio,
                                     Qt::FastTransformation);
            }
            Q_UNUSED(sessionApp);
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
    // @p image is always host-raw (workers no longer bake). Size-first ctor;
    // never QPixmap::fromImage of multi-MP in ImageItem(path, image).
    WorkspaceItemState app;
    if (applyStoredSessionCrop && isImageMode()) {
        // Always attempt seed from path XDG when bound. The old gate
        // !(haveId && !m_appearance.get(id)) *skipped* seed when the slot was
        // empty — which is exactly when durable rotate/flip must be loaded
        // after restart. appearanceForNewImageModeItem seeds then returns
        // identity only if XDG has nothing.
        if (m_currentSessionId != kInvalidSessionImageId
            || m_itemStates.contains(path)) {
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
            ? m_currentSessionId
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
            ? m_currentSessionId
            : item->sessionId();
        installDisplayPixels(item, image, kind, sid);
    }
    return item;
}


void ImageView::seedSessionAppearancesFromPaths(const QStringList &paths,
                                                   const QVector<SessionImageId> &ids)
{
    // Fresh session: allow seed again for new ids (old set cleared on invalidate).
    const int n = qMin(paths.size(), ids.size());
    for (int i = 0; i < n; ++i) {
        m_appearanceSeedAttempted.remove(ids.at(i));
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
    if (m_appearanceSeedAttempted.contains(sid)) {
        return;
    }
    m_appearanceSeedAttempted.insert(sid);
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
        m_appearanceSeedAttempted.insert(sid);
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
    m_appearanceSeedAttempted.insert(sid);
    if (m_appearance.contains(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (const WorkspaceItemState *cur = m_appearance.get(sid)) {
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
        // Cold open / ←→: id slot often empty until first seed. Path XDG holds
        // durable rotate/flip/grade — pull it before materialize or we paint
        // unoriented host forever.
        if (!SessionAppearance::hasContentAppearance(appearance)
            && appearance.colorAdjust.isIdentity()
            && !item->path().isEmpty()) {
            const_cast<ImageView *>(this)->seedSessionAppearanceFromState(
                id, item->path());
            if (const WorkspaceItemState *app = m_appearance.get(id)) {
                appearance = *app;
            }
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        const auto it = m_itemStates.constFind(item->path());
        if (it != m_itemStates.cend()) {
            appearance = *it;
        }
    }
    // Applied fingerprint is authoritative when the store lagged a live edit
    // (rotate then fitItem before m_appearance was visible to this reader).
    if (item->hasAppliedContentXform()) {
        const ContentXform::Value x = item->appliedContentXform();
        if (appearance.contentQuarterTurns == 0 && x.quarterTurns != 0) {
            appearance.contentQuarterTurns = x.quarterTurns;
        }
        if (!appearance.contentHFlip && x.hFlip) {
            appearance.contentHFlip = true;
        }
        if (!appearance.contentVFlip && x.vFlip) {
            appearance.contentVFlip = true;
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

DisplaySurface::State ImageView::displaySurfaceStateForItem(const ImageItem *item,
                                                            int hostLongEdge,
                                                            bool climbPending) const
{
    DisplaySurface::State ds;
    if (!item) {
        return ds;
    }
    ds.climbPending = climbPending;
    ds.hostLongEdge = hostLongEdge >= 0
        ? hostLongEdge
        : (item->path().isEmpty()
               ? 0
               : ImageCache::longEdge(ImageCache::get(item->path())));
    ds.want = ContentXform::Value::fromState(
        wantAppearanceForItem(item, item->sessionId()));
    if (item->hasDisplayPixels()) {
        ds.haveDisplayEdge = item->displayPixelLongEdge();
        ds.attachedKind = item->hasDecodedPixels()
            ? DisplaySurface::AttachedKind::FullSource
            : DisplaySurface::AttachedKind::SoftPreview;
        if (item->hasAppliedContentXform()) {
            ds.applied = item->appliedContentXform();
        }
    }
    if (isImageMode()) {
        // ImageFocus: on-screen (window) need only. Forcing need ≥ file native
        // jumped straight to Full and skipped Soft→Prefer progressive installs.
        // Zoom / 1:1 raises on-screen need; climb then escalates.
        const int need = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
        ds.needEdge = cappedDisplayEdgeForPath(item->path(), need);
    } else if (isGalleryMode()) {
        ds.needEdge = galleryDisplayEdgeForItem(item, /*allowHighRes=*/true);
    } else if (isWorkspaceMode()) {
        ds.needEdge = itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
    }
    return ds;
}

bool ImageView::applyDisplaySurfaceAction(ImageItem *item,
                                          const DisplaySurface::Action &act,
                                          const QImage &hostSample,
                                          int fallbackNeedEdge,
                                          PathRasterService::ClimbPolicy climbPolicy)
{
    if (!item) {
        return false;
    }
    using AT = DisplaySurface::ActionType;
    if (act.type == AT::None) {
        return false;
    }
    const QString path = item->path();
    if (path.isEmpty()) {
        return false;
    }
    if (act.type == AT::ScheduleClimb) {
        if (!m_pathRaster) {
            return false;
        }
        const int need = act.climbNeedEdge > 0 ? act.climbNeedEdge : fallbackNeedEdge;
        if (need > 0) {
            m_pathRaster->ensure(path, need, logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::ScheduleAsyncMaterialize) {
        scheduleAsyncHostRematerialize(
            path, item->sessionId(),
            wantAppearanceForItem(item, item->sessionId()));
        // Intermediate Prefer host is baking async; keep Soft→Prefer→Full climb
        // when on-screen need is still above host (Workspace zoom-in).
        const int need = fallbackNeedEdge > 0 ? fallbackNeedEdge : 0;
        if (m_pathRaster && need > 0
            && !coversEdge(item->displayPixelLongEdge(), need)
            && !coversEdge(ImageCache::longEdge(ImageCache::get(path)), need)) {
            m_pathRaster->ensure(path, need, logicalSizeForPath(path), climbPolicy);
        }
        return false;
    }
    if (act.type == AT::AttachSoft || act.type == AT::AttachFull) {
        QImage host = hostSample;
        if (host.isNull()) {
            host = ImageCache::get(path);
        }
        if (host.isNull()) {
            return false;
        }
        const auto kind = (act.type == AT::AttachSoft)
            ? SessionAppearance::PixelKind::SoftPreview
            : SessionAppearance::PixelKind::FullSource;
        if (!canAcceptDisplaySample(item, host, kind)) {
            return false;
        }
        const QSize before = item->imageSize();
        if (isImageMode()) {
            installImageModeSampleInPlace(item, path, host, kind);
        } else {
            installDisplayPixels(item, host, kind, item->sessionId());
            item->update();
            if (m_scene) {
                m_scene->update(item->sceneBoundingRect());
            }
        }
        const bool sizeChanged = (item->imageSize() != before);
        if (act.type == AT::AttachSoft) {
            syncItemDisplaySurface(item, ImageCache::longEdge(host),
                m_pathRaster && m_pathRaster->isClimbPending(path));
            const DisplaySurface::SurfaceId sid =
                static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
            const DisplaySurface::Action again =
                (sid != DisplaySurface::kInvalidSurfaceId)
                    ? m_displaySurfaces.evaluate(sid)
                    : DisplaySurface::decide(
                          displaySurfaceStateForItem(
                              item, ImageCache::longEdge(host), false));
            if (again.type == AT::ScheduleAsyncMaterialize) {
                scheduleAsyncHostRematerialize(
                    path, item->sessionId(),
                    wantAppearanceForItem(item, item->sessionId()));
            } else if (again.type == AT::ScheduleClimb && m_pathRaster) {
                const int need = again.climbNeedEdge > 0 ? again.climbNeedEdge
                                                         : fallbackNeedEdge;
                if (need > 0) {
                    m_pathRaster->ensure(path, need, logicalSizeForPath(path),
                                         climbPolicy);
                }
            }
        }
        return sizeChanged;
    }
    return false;
}

bool ImageView::canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                       SessionAppearance::PixelKind kind) const
{
    if (!item || pixels.isNull()) {
        return false;
    }
    const int incoming = ImageCache::longEdge(pixels);
    if (incoming <= 0) {
        return false;
    }
    // Soft must not demote FullSource (also enforced by decide Soft path).
    if (kind == SessionAppearance::PixelKind::SoftPreview && item->hasDecodedPixels()) {
        return false;
    }
    if (!item->hasDisplayPixels()) {
        return true;
    }
    DisplaySurface::State ds = displaySurfaceStateForItem(item, incoming, false);
    if (isCropDraftLockedItem(item) || isCropDraftLockedPath(item->path())) {
        ds.frozen = true;
    }
    const DisplaySurface::Action act = DisplaySurface::decide(ds);
    using AT = DisplaySurface::ActionType;
    if (act.type == AT::None) {
        return false;
    }
    if (kind == SessionAppearance::PixelKind::SoftPreview) {
        return act.type == AT::AttachSoft
            || act.type == AT::ScheduleAsyncMaterialize;
    }
    // FullSource: accept when decide wants full attach or async full bake.
    return act.type == AT::AttachFull
        || act.type == AT::ScheduleAsyncMaterialize
        || act.type == AT::AttachSoft;
}

void ImageView::installDisplayPixels(ImageItem *item, const QImage &pixels,
                                     SessionAppearance::PixelKind kind,
                                     SessionImageId sid)
{
    if (!item) {
        return;
    }
    const QString path = item->path();
    // Crop draft owns the live sample — ladder/async must not replace it
    // (store want still has crop → wrong bake; soft↔full thrash).
    if (isCropDraftLockedItem(item) || isCropDraftLockedPath(path)) {
        return;
    }
    if (!canAcceptDisplaySample(item, pixels, kind)) {
        return;
    }
    if (!pixels.isNull()) {
        TtfpTrace::noteFirstPixels("installDisplayPixels");
    }
    const QSize layoutBefore = item->imageSize();

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

    // Host cache is unoriented. Every ladder/decode sample that enters here is
    // host-raw (Gallery, Image, Workspace). Display-ready stash soft never
    // enters this function — pendingTile attaches it via attachDisplaySample.
    //
    // Invariant: for session-bound tiles, attach only materializeDisplay(host,
    // store want). Never attach host under a content want. Never set applied
    // xform unless the attached pixels match that bake.
    const bool wantBake =
        SessionAppearance::hasContentAppearance(appearance)
        || !appearance.colorAdjust.isIdentity();
    if (!path.isEmpty()) {
        // Incoming is always unoriented host — keep ImageCache pure.
        ImageCache::put(path, pixels);
    }

    // raw → optional gallery soft clamp → materializeDisplay → attach.
    QImage pixelsForDisplay = pixels;
    if (isGalleryMode() && kind == SessionAppearance::PixelKind::SoftPreview) {
        pixelsForDisplay = clampSoftForGalleryCell(
            pixels,
            galleryDisplayEdgeForItem(item, /*allowHighRes=*/true),
            ThumtooCache::kFilmstripLadderEdge);
    }
    QImage display = pixelsForDisplay;
    SessionAppearance::PixelKind attachKind = kind;
    bool scheduleFullBake = false;
    if (wantBake) {
        const int maxGui = ContentXform::kGuiMaterializeMaxEdge;
        const int hostEdge = ImageCache::longEdge(pixelsForDisplay);
        // Multi-MP host cannot materialize on the GUI thread. Soft stand-in
        // (clamp ≤512 + SoftPreview bake) keeps crop/orient visible now;
        // full-resolution bake is scheduled async. Never attach raw host.
        if (hostEdge > maxGui) {
            pixelsForDisplay = ImageCache::clampToMaxEdge(pixelsForDisplay, maxGui);
            attachKind = SessionAppearance::PixelKind::SoftPreview;
            // Only escalate to full async bake when the caller asked for FullSource.
            scheduleFullBake = (kind == SessionAppearance::PixelKind::FullSource);
        }
        const int edge = ImageCache::longEdge(pixelsForDisplay);
        if (edge <= 0) {
            scheduleAsyncHostRematerialize(path, sid, appearance);
            return;
        }
        if (edge > maxGui) {
            // Clamp failed oddly — still do not attach host under want.
            scheduleAsyncHostRematerialize(path, sid, appearance);
            return;
        }
        display = SessionAppearance::materializeDisplay(
            pixelsForDisplay, appearance, attachKind);
        if (display.isNull()) {
            scheduleAsyncHostRematerialize(path, sid, appearance);
            return;
        }
        // Soft attach is ignored while FullSource is present.
        if (attachKind == SessionAppearance::PixelKind::SoftPreview
            && item->hasDecodedPixels()) {
            item->clearDecodedPixels();
        }
    }
    const QSize sizeBeforeAttach = item->imageSize();
    attachDisplaySample(item, display, appearance, attachKind);
    if (scheduleFullBake) {
        scheduleAsyncHostRematerialize(path, sid, appearance);
    }
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
    registerItemDisplaySurface(item);
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
    QImage pixels = slideshowRaster(path);
    // Filmstrip often has Soft while ImageCache only has size-probe LQIP (or
    // LRU-evicted the soft). Prefer strip / shared host sample first.
    if (pixels.isNull() && m_imageModeSoftProvider) {
        bool ready = false;
        pixels = m_imageModeSoftProvider(path, m_currentSessionId, &ready);
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
    if (m_currentSessionId != kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_appearance.get(m_currentSessionId)) {
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
        const bool sameId = (m_currentSessionId != kInvalidSessionImageId
                             && cand->sessionId() == m_currentSessionId);
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

    if (!isImageMode() || path.isEmpty()) {
        return;
    }
    // Slideshow owns the viewport with dwell/live blits. Pending tile used to
    // clearLiveCanvas + cancelSlideshowMotion after fade-end cleared the hold,
    // wiping the dwell we just armed. Underlay is hidden for the whole show.
    if (m_slideshowProgressActive) {
        return;
    }
    if (isCropDraftLockedPath(path)) {
        return;
    }

    bool displayReady = false;
    QImage pixels = resolveImageModePendingPixels(path, preview, &displayReady);
    biltooLoadDbg("pendingTile path=%s soft=%dx%d cache=%d displayReady=%d",
                  qPrintable(QFileInfo(path).fileName()),
                  pixels.width(), pixels.height(),
                  ImageCache::has(path) ? 1 : 0, displayReady ? 1 : 0);

    // Cold path: no soft/LQIP yet. Within the *same* path, keep the prior frame
    // until soft arrives (avoids a flash on PreferCache gaps). Different path
    // (session switch / ←→): never keep the previous file's pixels under a new
    // contentRect — that is the wrong-pixels stretch. Layout without pixels is
    // fine (blank/placeholder at the correct aspect); only the old sample is not.
    if (pixels.isNull()) {
        if (m_items.size() == 1) {
            ImageItem *item = m_items.first();
            const bool pathChanged = item->path() != path;
            item->setPath(path);
            bindImageModeSessionCursor(item);
            if (pathChanged) {
                // Soft OR full — hasDecodedPixels is full-only and left prior soft
                // in place so canAccept rejected the next path's smaller LQIP.
                if (item->hasDisplayPixels()) {
                    item->clearDecodedPixels();
                }
                item->setSessionCrop(false, QRect());
                item->setContentHFlip(false);
                item->setContentVFlip(false);
                item->setColorAdjustmentsRecord(ColorAdjustments{});
                item->clearAppliedContentXform();
                const QSize sz = layoutSizeForPath(path, QImage());
                if (isPositiveSize(sz)) {
                    item->setIntrinsicSize(sz);
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
        const bool pathChanged = (item->path() != path);
        if (pathChanged) {
            captureStickyPanAnchor(item);
        }
        item->setPath(path);
        bindImageModeSessionCursor(item);
        // Path change: drop prior sample AND content chrome. wantAppearanceForItem
        // merges item->sessionHasCrop / contentHFlip when the store slot is empty;
        // leaking the previous image's crop into the new soft is the ←/→ stretch.
        if (pathChanged) {
            // Soft OR full. hasDecodedPixels is full-only; leaving prior soft
            // made canAccept reject the next path's LQIP (shown edge ≥ incoming).
            if (item->hasDisplayPixels()) {
                item->clearDecodedPixels();
            }
            item->setSessionCrop(false, QRect());
            item->setContentHFlip(false);
            item->setContentVFlip(false);
            item->setColorAdjustmentsRecord(ColorAdjustments{});
            item->clearAppliedContentXform();
        } else if (item->hasDecodedPixels()) {
            // Same path soft→HQ: clear full so soft can attach.
            item->clearDecodedPixels();
        }
        // Host-raw soft: installDisplayPixels seeds ImageCache + materializes want.
        // Display-ready (stashed Gallery / filmstrip override) only when it still
        // matches store want — otherwise rematerialize from host so crop/rotate
        // in SessionAppearanceStore are not skipped (stale strip Soft looked like
        // "edits not persistent").
        const WorkspaceItemState want = wantAppearanceForItem(item, item->sessionId());
        if (displayReady && SessionAppearance::hasContentAppearance(want)) {
            const QImage host = ImageCache::get(path);
            if (!host.isNull()) {
                pixels = host;
                displayReady = false;
            }
        }
        if (displayReady) {
            attachDisplaySample(item, pixels, want,
                                SessionAppearance::PixelKind::SoftPreview);
        } else {
            installDisplayPixels(item, pixels, SessionAppearance::PixelKind::SoftPreview,
                                 item->sessionId());
        }

        // Intrinsic only from definitive file size — never from soft/LQIP sz.
        const QSize known = logicalSizeForPath(path);
        QSize targetSize = item->imageSize();
        if (isPositiveSize(known) && known.width() > 1 && known.height() > 1
            && !isProvisionalImageSize(path)) {
            targetSize = ContentXform::layoutSize(known, want);
        }
        int didFit = 0;
        if (isPositiveSize(targetSize) && targetSize.width() > 1
            && !isProvisionalImageSize(path)) {
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

    // Image mode: swap best in-process soft/LQIP immediately (no IPC, no pool).
    if (role == LoadReplace && isImageMode()) {
        installImageModePendingTile(path);
        // Rapid ←/→: stop here. No PreferCache, no classic decode, no escalate —
        // those race the next key and stall the GUI. Settle timer (MainWindow
        // ~80ms quiet) clears nav-hot and calls loadImage again for climb.
        if (m_slideshowNavHot) {
            const int edge = imageModeItemForPath(path)
                ? imageModeItemForPath(path)->displayPixelLongEdge()
                : 0;
            biltooLoadDbg("PATH nav-hot soft-only path=%s edge=%d",
                          qPrintable(QFileInfo(path).fileName()), edge);
            return;
        }
        if (ImageItem *it = imageModeItemForPath(path)) {
            if (it->displayPixelLongEdge() > 0) {
                const QImage soft = it->displayImage();
                const QString pathCopy = path;
                // One frame for soft paint, then PreferCache for the settled path.
                QTimer::singleShot(16, this, [this, pathCopy, soft]() {
                    if (!isImageMode() || classicPath() != pathCopy) {
                        return;
                    }
                    if (m_slideshowNavHot) {
                        return;
                    }
                    ensureImageModeQualityClimb(pathCopy, soft);
                });
                return;
            }
        }
    }

    // Image mode: do not SoftOnly/PreferCache on cold open — LQIP (if cached)
    // + tiles. Other modes still seed Soft via scheduleClassicImageDecode.

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
    // Image mode: LQIP/cache underlay only if already present, then tiles.
    // No SoftOnly encode and no PreferCache/Full climb in parallel with tiles.
    if (isImageMode() && !m_slideshowProgressActive && !m_slideshowNavHot
        && role == LoadReplace) {
        // Size probe so tileNativeSize / layout can resolve (no soft encode).
        ThumtooCache::scheduleProbe(path);
        tickPrimaryTileLod(12);
        Q_UNUSED(gen);
        return;
    }

    // Slideshow / non–Image-mode: soft stand-in job still used for fast paint.
    const QPointer<ImageView> guard(this);
    const int roleInt = static_cast<int>(role);
    const int softEdge = ThumtooCache::kGalleryLadderEdge;
    const WorkspaceItemState sessionApp = appearanceForNewImageModeItem(path);

    startSoftPreviewJob(guard, path, gen, roleInt, softEdge, sessionApp);
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
    // Tiles own the cell — no soft/PreferCache HOST climb in parallel.
    for (ImageItem *ii : m_items) {
        if (ii && ii->path() == path && ii->tileLodWanted()) {
            // Drop soft inflight accounting so gallery concurrency budget is
            // not held by a climb we no longer want; tickPrimaryTileLod cancels
            // PathRaster once per path via m_tileLodPreferCancelled.
            auto sit = m_gallerySoft.find(path);
            if (sit != m_gallerySoft.end()) {
                clearGallerySoftInflight(*sit);
            }
            tickPrimaryTileLod(12);
            return;
        }
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
    // Stale inflight after fast scroll / empty ladder delivery must not block
    // soft growth. Only hold the slot while have still covers the inflight edge.
    if (st.inflight > 0) {
        if (st.have > 0 && coversEdge(st.have, st.inflight)) {
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

    // PreferCache/FocusFull may have co-built durable tiles; wake tile LOD only
    // if some on-canvas item for this path already wants tiles (avoids a full
    // tickPrimary scan on every soft delivery).
    if (isImageMode() || isWorkspaceMode() || isGalleryMode()) {
        for (ImageItem *ii : m_items) {
            if (ii && ii->path() == path && ii->tileLodWanted()) {
                tickPrimaryTileLod(12);
                break;
            }
        }
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
    // Ladder samples are host-raw. installDisplayPixels materializes want
    // (soft stand-in ≤512 + async full for multi-MP). Never bake here and
    // put the result into ImageCache — that double-applied crop.
    Q_UNUSED(maxEdge);
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
    // Tile LOD for workspace items that need past soft max.
    tickPrimaryTileLod(8);

    for (ImageItem *ii : targets) {
        if (!ii) {
            continue;
        }
        const QString path = ii->path();
        if (path.isEmpty()) {
            continue;
        }
        if (isCropDraftLockedPath(path)) {
            continue;
        }
        // Deep zoom: tiles own display; skip PreferCache whole-frame climb.
        if (ii->tileLodWanted()) {
            continue;
        }
        // Always measure need after the current view transform (zoom/pan).
        const int needEdge = itemOnScreenNeedEdge(ii, /*allowHighRes=*/true);
        const int have = ii->displayPixelLongEdge();
        if (needEdge > 0 && have > 0 && coversEdge(have, needEdge)) {
            continue;
        }
        const bool pending = m_pathRaster->isClimbPending(path);
        syncItemDisplaySurface(ii, -1, pending);
        const DisplaySurface::SurfaceId sid =
            static_cast<DisplaySurface::SurfaceId>(ii->displaySurfaceId());
        if (sid != DisplaySurface::kInvalidSurfaceId) {
            m_displaySurfaces.setNeed(sid, needEdge);
        }
        DisplaySurface::Action act =
            (sid != DisplaySurface::kInvalidSurfaceId)
                ? m_displaySurfaces.evaluate(sid)
                : DisplaySurface::decide(
                      displaySurfaceStateForItem(ii, -1, pending));
        if (act.type == DisplaySurface::ActionType::ScheduleClimb
            && act.climbNeedEdge < needEdge) {
            act.climbNeedEdge = needEdge;
        }
        // Decide may return None while still short (stale settle / Full shortfall).
        // Force PathRaster escalate so Soft→Prefer→Full continues on zoom-in.
        if (act.type == DisplaySurface::ActionType::None
            && needEdge > 0 && !coversEdge(have, needEdge) && !pending) {
            act.type = DisplaySurface::ActionType::ScheduleClimb;
            act.climbNeedEdge = needEdge;
        }
        (void)applyDisplaySurfaceAction(
            ii, act, QImage(), needEdge,
            PathRasterService::ClimbPolicy::EscalateToFull);
        if (!coversEdge(ii->displayPixelLongEdge(), needEdge)
            && (m_pathRaster->isGaveUp(path)
                || (!m_pathRaster->isClimbPending(path)
                    && act.type == DisplaySurface::ActionType::ScheduleClimb))) {
            // PreferCache plateau short of on-screen need after zoom — native decode.
            if (m_pathRaster->isGaveUp(path)
                || !m_pathRaster->isClimbPending(path)) {
                scheduleImageModeNativeDecodeOnce(path);
            }
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

    // Gallery / Workspace: DisplaySurface::decide per item (SessionImageId want).
    // Avoid host-vs-shown shouldUpgrade on cropped FullSource (pulse / no-op spam).
    const int incoming = ImageCache::longEdge(image);
    bool gallerySizeChanged = false;
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
                ? m_displaySurfaces.evaluate(sid)
                : DisplaySurface::decide(ds);
        const auto pol = isWorkspaceMode()
            ? PathRasterService::ClimbPolicy::EscalateToFull
            : PathRasterService::ClimbPolicy::SoftDisplay;
        if (applyDisplaySurfaceAction(item, act, image, ds.needEdge, pol)) {
            gallerySizeChanged = true;
        }
    }
    if (gallerySizeChanged && isGalleryMode() && m_layoutMode != LayoutMode::FreeForm) {
        applyLayout(GalleryPackReason::ContentChange);
    } else if (viewport()) {
        viewport()->update();
    }
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
    // Host is in ImageCache / item. Materialize store want (soft stand-in +
    // async multi-MP). Do not bake chrome-only on multi-MP — cannot
    // bake crop on the GUI and used to claim applied == want without pixels.
    if (SessionAppearance::hasContentAppearance(app)
        || !app.colorAdjust.isIdentity()) {
        rematerializeItemContent(item, app);
    }
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
    const int incoming = ImageCache::longEdge(image);
    for (ImageItem *existing : m_items) {
        if (!existing || existing->path() != path) {
            continue;
        }
        ++have;
        // Soft was wrongly stored as "decoded"; still accept stricter long edge.
        if (!existing->hasDecodedPixels()
            || existing->shouldUpgradeDisplayTo(incoming)) {
            if (installFullPreservingWorkspaceFootprint(existing, image)) {
                sizeChanged = true;
            } else if (existing->shouldUpgradeDisplayTo(incoming)) {
                // Footprint helper no-ops once hasDecodedPixels; force upgrade.
                installDisplayPixels(existing, image,
                                     SessionAppearance::PixelKind::FullSource,
                                     existing->sessionId());
                existing->update();
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
    // Content 90°/flip/crop are materialize()'d in createItemFromImage when want is set.
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
    // createItemFromImage materializes host × store want (install invariant).
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
    if (isCropDraftLockedPath(path)) {
        return;
    }
    // Deep-zoom tile band: grid tiles own display; skip PreferCache/Full climb.
    // Check Image-mode primary and any Workspace/Gallery item for this path.
    if (ImageItem *it = imageModeItemForPath(path)) {
        if (it->tileLodWanted()) {
            return;
        }
    }
    for (ImageItem *ii : m_items) {
        if (ii && ii->path() == path && ii->tileLodWanted()) {
            return;
        }
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
    // Never ImageLoader::load on the GUI thread — that was the crop-enter stall
    // on multi-MP files. Callers use the best available sample (cache / item)
    // and schedule requestCropFullRaster / PathRaster for a native upgrade.
    return cached;
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
    if (isCropDraftLockedPath(path)) {
        return;
    }
    if (m_slideshowProgressActive) {
        return;
    }
    // Tiles own the soft/Prefer/Full band once on-screen long edge exceeds one
    // tile side — do not schedule SoftOnly/PreferCache/Full in parallel.
    if (ImageItem *it = imageModeItemForPath(path)) {
        if (it->tileLodWanted()) {
            tickPrimaryTileLod(12);
            return;
        }
    }
    if (!sample.isNull() && sampleCoversNativeLogical(path, sample)) {
        return;
    }

    const int need = imageModeOnScreenNeedEdge();
    const int have = sample.isNull() ? 0 : ImageCache::longEdge(sample);
    // Climb to on-screen need (ladder-capped), not file native. Native is
    // requested only when the window actually needs it (zoom / large view).
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
    // @p image is host-raw (soft job, ladder, quality job). Single materialize
    // in installDisplayPixels — soft stand-in + async full when multi-MP want.
    const SessionAppearance::PixelKind kind = pixelKindForImageModeSample(path, image);
    const bool ok = tryInstallImageModeSampleBaked(path, image, kind);
    // Decide soft→async / climb from the new host edge (event-driven).
    driveImageFocusSurface();
    return ok;
}

bool ImageView::tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                               SessionAppearance::PixelKind kind)
{
    // Name is historical: @p image is host-raw. installDisplayPixels materializes.
    if (!isImageMode() || path.isEmpty() || image.isNull()) {
        return false;
    }
    if (isCropDraftLockedPath(path)) {
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

void ImageView::scheduleTileLodAfterInteraction(int delayMs)
{
    if (!m_tileLodZoomDebounce) {
        m_tileLodZoomDebounce = new QTimer(this);
        m_tileLodZoomDebounce->setSingleShot(true);
        connect(m_tileLodZoomDebounce, &QTimer::timeout, this, [this]() {
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
    m_tileLodZoomDebounce->setInterval(qMax(0, delayMs));
    m_tileLodZoomDebounce->start();
}

void ImageView::tickPrimaryTileLod(int budget)
{
    if (m_slideshowProgressActive) {
        return;
    }
    QList<ImageItem *> targets;
    if (isImageMode()) {
        ImageItem *item = imageModeItemForPath(classicPath());
        if (!item) {
            item = targetItem();
        }
        if (item) {
            targets.append(item);
        }
    } else if (isWorkspaceMode()) {
        // Prefer selection; else on-canvas items. Bound to 8 with in-view first
        // (same starvation class as Gallery list-order).
        QList<ImageItem *> pool;
        if (m_scene) {
            for (QGraphicsItem *gi : m_scene->selectedItems()) {
                if (auto *ii = qgraphicsitem_cast<ImageItem *>(gi)) {
                    if (!ii->path().isEmpty()) {
                        pool.append(ii);
                    }
                }
            }
        }
        if (pool.isEmpty()) {
            for (ImageItem *ii : m_items) {
                if (!ii || ii->path().isEmpty()) {
                    continue;
                }
                pool.append(ii);
            }
        }
        QRectF sceneVis;
        if (m_scene) {
            sceneVis = mapToScene(viewport()->rect()).boundingRect();
        }
        struct Cand {
            ImageItem *item = nullptr;
            bool inView = false;
            bool wanted = false;
            int need = 0;
        };
        QVector<Cand> cands;
        cands.reserve(pool.size());
        for (ImageItem *ii : pool) {
            Cand c;
            c.item = ii;
            c.inView = sceneVis.isNull()
                || ii->sceneBoundingRect().intersects(sceneVis);
            c.wanted = ii->tileLodWanted();
            c.need = itemOnScreenNeedEdge(ii, /*allowHighRes=*/true);
            cands.append(c);
        }
        std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
            if (a.wanted != b.wanted) {
                return a.wanted;
            }
            if (a.inView != b.inView) {
                return a.inView;
            }
            return a.need > b.need;
        });
        for (const Cand &c : cands) {
            targets.append(c.item);
            if (targets.size() >= 8) {
                break;
            }
        }
    } else if (isGalleryMode()) {
        // Large cells only (tileLodWanted); prefer those in the view, then by
        // on-screen long edge. List order alone starved late cells when >8
        // cells were past the soft threshold.
        struct Cand {
            ImageItem *item = nullptr;
            bool inView = false;
            qreal screenLong = 0;
        };
        QRectF sceneVis;
        if (m_scene) {
            sceneVis = mapToScene(viewport()->rect()).boundingRect();
        }
        QVector<Cand> cands;
        cands.reserve(16);
        for (ImageItem *ii : m_items) {
            if (!ii || ii->path().isEmpty() || !ii->tileLodWanted()) {
                continue;
            }
            Cand c;
            c.item = ii;
            c.inView = sceneVis.isNull()
                || ii->sceneBoundingRect().intersects(sceneVis);
            QSizeF cell = ii->galleryCellSize();
            if (cell.isEmpty()) {
                const QRectF br = ii->sceneBoundingRect();
                cell = QSizeF(br.width(), br.height());
            }
            qreal viewScale = 1.0;
            qreal dpr = 1.0;
            if (QWidget *vp = viewport()) {
                dpr = vp->devicePixelRatioF();
            }
            {
                const QTransform vt = transform();
                // Axis-aligned gallery view: m11/m22 are sufficient (no shear).
                viewScale = qMax(0.01, qMax(qAbs(vt.m11()), qAbs(vt.m22())));
            }
            if (!(dpr > 0.0)) {
                dpr = 1.0;
            }
            const qreal cellLong = qMax(cell.width(), cell.height());
            c.screenLong = cellLong * viewScale * dpr;
            cands.append(c);
        }
        std::sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
            if (a.inView != b.inView) {
                return a.inView;
            }
            return a.screenLong > b.screenLong;
        });
        for (const Cand &c : cands) {
            targets.append(c.item);
            if (targets.size() >= 8) {
                break;
            }
        }
        if (targets.isEmpty()) {
            return;
        }
    } else {
        return;
    }

    bool anyWanted = false;
    bool anyIncomplete = false;
    // Collect items that will receive issue budget this tick.
    QList<ImageItem *> issueTargets;
    issueTargets.reserve(targets.size());
    for (ImageItem *item : targets) {
        if (!item) {
            continue;
        }
        // Crop draft freeze: no tile climb (mirrors PreferCache lock).
        if (isCropDraftLockedItem(item) || item->tileLodSuppressed()) {
            continue;
        }
        const QString path = item->path();
        if (item->tileLodWanted()) {
            anyWanted = true;
            // Cancel PreferCache once per path when entering the tile band —
            // not every tick (avoids thrashing PathRaster state).
            if (m_pathRaster && !path.isEmpty()
                && !m_tileLodPreferCancelled.contains(path)) {
                m_pathRaster->cancel(path);
                m_tileLodPreferCancelled.insert(path);
            }
            issueTargets.append(item);
        } else if (!path.isEmpty()) {
            m_tileLodPreferCancelled.remove(path);
        }
    }
    // Split the global issue budget across items. Previously each of up to 8
    // targets got the full budget (8×8 = 64 concurrent cell requests / tick),
    // which flooded thumtoo workers with scale-0 / pyramid work.
    int remaining = qMax(0, budget);
    for (int i = 0; i < issueTargets.size(); ++i) {
        ImageItem *item = issueTargets.at(i);
        const int left = issueTargets.size() - i;
        // Earlier targets are higher priority (in-view / larger); give them a
        // fair share of what remains, at least 1 while budget lasts.
        const int share = remaining > 0 ? qMax(1, remaining / left) : 0;
        if (share > 0) {
            item->tickTileLod(share);
            remaining -= share;
        } else {
            // Still pump completions / plan without new issues.
            item->tickTileLod(0);
        }
        if (!item->tileLodViewportCovered()) {
            anyIncomplete = true;
        }
    }

    if (anyWanted) {
        static int s_tileDbg = -1;
        if (s_tileDbg < 0) {
            const char *e = std::getenv("BILTOO_TILE_DEBUG");
            s_tileDbg = (e && e[0] && e[0] != '0') ? 1 : 0;
        }
        if (s_tileDbg) {
            for (ImageItem *item : targets) {
                if (!item || !item->tileLodWanted()) {
                    continue;
                }
                fprintf(stderr,
                        "biltoo/tile: path=%s wanted=1 covered=%d active=%d\n",
                        qPrintable(item->path()),
                        item->tileLodViewportCovered() ? 1 : 0,
                        item->tileLodActive() ? 1 : 0);
            }
        }
    }

    // Fast pump (33ms) while incomplete; slow heartbeat (250ms) while covered
    // so pan can discover new cells without a continuous high-rate timer.
    // Stop entirely when no item wants tiles.
    if (anyWanted) {
        if (!m_tileLodTimer) {
            m_tileLodTimer = new QTimer(this);
            connect(m_tileLodTimer, &QTimer::timeout, this, [this]() {
                // Image mode: one focus item can use a fuller batch.
                // Gallery/Workspace: modest global budget (split across ≤8 targets).
                // (Name avoids shadowing tickPrimaryTileLod's budget parameter —
                // this connect lives inside that function.)
                const int issueBudget = isImageMode() ? 12 : 6;
                tickPrimaryTileLod(issueBudget);
            });
        }
        // 50ms while filling (~20Hz): 33ms competed with input under load.
        const int interval = anyIncomplete ? 50 : 250;
        if (m_tileLodTimer->interval() != interval) {
            m_tileLodTimer->setInterval(interval);
        }
        if (!m_tileLodTimer->isActive()) {
            m_tileLodTimer->start();
        }
    } else if (m_tileLodTimer && m_tileLodTimer->isActive()) {
        m_tileLodTimer->stop();
    }
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
    // Crop draft freezes the sample — do not schedule soft↔full climb.
    if (isCropDraftLockedPath(path)) {
        return;
    }

    // Tile LOD owns deep zoom when on-screen need exceeds soft max (TILE_LOD).
    // PreferCache whole-frame climb is skipped for that band; soft/LQIP stays
    // as underlay until tiles arrive.
    tickPrimaryTileLod(8);
    if (item->tileLodWanted()) {
        driveImageFocusSurface();
        return;
    }

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

    // Moderate zoom still on soft band: PathRaster Soft→PreferCache→Full.
    scheduleImageModePreferCacheClimb(path, need);
    // Soft matching want + large host → ScheduleAsyncMaterialize via decide.
    driveImageFocusSurface();
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
            // upgrade Image mode (soft→HQ). Skip when tiles already own zoom.
            bool tilesOwn = false;
            if (ImageItem *it = imageModeItemForPath(path)) {
                tilesOwn = it->tileLodWanted();
            }
            if (!tilesOwn) {
                scheduleImageModePreferCacheClimb(path, ThumtooCache::kBatchOverviewEdge);
            }
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

void ImageView::ensureImageFocusSurface()
{
    if (!isImageMode()) {
        if (m_imageFocusSurface != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = primaryItem();
    if (!item || item->path().isEmpty()) {
        m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;
        return;
    }
    // Prefer the canvas item registry (create/destroy lifecycle).
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    // Kind may have been GalleryTile if item was created before mode switch.
    const auto itemSid =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    const DisplaySurface::Binding *ib = m_displaySurfaces.binding(itemSid);
    if (ib && ib->kind != DisplaySurface::Kind::ImageFocus) {
        registerItemDisplaySurface(item); // rebind as ImageFocus
    }
    m_imageFocusSurface =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
}

void ImageView::syncImageFocusSurfaceState()
{
    ensureImageFocusSurface();
    if (m_imageFocusSurface == DisplaySurface::kInvalidSurfaceId) {
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
    if (m_cropDraftSampleFrozen && isCropDraftLockedPath(path)) {
        m_displaySurfaces.setFrozen(m_imageFocusSurface, true);
    }
}

void ImageView::driveImageFocusSurface()
{
    if (!isImageMode() || m_slideshowProgressActive) {
        return;
    }
    syncImageFocusSurfaceState();
    if (m_imageFocusSurface == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    const DisplaySurface::Binding *b =
        m_displaySurfaces.binding(m_imageFocusSurface);
    if (!b) {
        return;
    }
    const DisplaySurface::Action action = m_displaySurfaces.evaluate(m_imageFocusSurface);
    const QString path = b->path;
    if (path.isEmpty()) {
        return;
    }
    ImageItem *item = primaryItem();
    if (!item || item->path() != path) {
        return;
    }
    SessionImageId sid = b->sessionId;
    if (sid == kInvalidSessionImageId) {
        sid = item->sessionId() != kInvalidSessionImageId ? item->sessionId()
                                                          : m_currentSessionId;
    }

    Q_UNUSED(sid);
    (void)applyDisplaySurfaceAction(
        item, action, QImage(),
        cappedDisplayEdgeForPath(path, 0),
        PathRasterService::ClimbPolicy::EscalateToFull);
}

void ImageView::registerItemDisplaySurface(ImageItem *item)
{
    if (!item || item->path().isEmpty()) {
        return;
    }
    unregisterItemDisplaySurface(item);
    DisplaySurface::Kind kind = DisplaySurface::Kind::GalleryTile;
    if (isWorkspaceMode()) {
        kind = DisplaySurface::Kind::WorkspaceItem;
    } else if (isImageMode()) {
        kind = DisplaySurface::Kind::ImageFocus;
    }
    const DisplaySurface::SurfaceId id = m_displaySurfaces.bind(
        kind, item->path(), item->sessionId());
    item->setDisplaySurfaceId(id);
}

void ImageView::unregisterItemDisplaySurface(ImageItem *item)
{
    if (!item) {
        return;
    }
    const qint64 sid = item->displaySurfaceId();
    if (sid == 0) {
        return;
    }
    m_displaySurfaces.unbind(static_cast<DisplaySurface::SurfaceId>(sid));
    item->setDisplaySurfaceId(0);
}

void ImageView::syncItemDisplaySurface(ImageItem *item, int hostLongEdge,
                                       bool climbPending)
{
    if (!item) {
        return;
    }
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    const DisplaySurface::SurfaceId id =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    if (id == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    const DisplaySurface::State ds =
        displaySurfaceStateForItem(item, hostLongEdge, climbPending);
    m_displaySurfaces.setNeed(id, ds.needEdge);
    m_displaySurfaces.setFrozen(id, ds.frozen);
    m_displaySurfaces.setHostLongEdge(id, ds.hostLongEdge);
    m_displaySurfaces.setClimbPending(id, ds.climbPending);
    m_displaySurfaces.setWant(id, ds.want);
    m_displaySurfaces.setAttached(id, ds.attachedKind, ds.haveDisplayEdge,
                                  ds.applied);
}
