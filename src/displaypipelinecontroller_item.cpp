// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "displaypipelinecontroller.h"
#include "itemcomponents.h"
#include "displaypipeline_jobs.h"

#include "tile_load_coordinator.h"

#include "imageview.h"
#include "imageitem.h"
#include "displayedgepolicy.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"
#include "pagepath.h"
#include "gallerysoftsm.h"
#include "displayquality.h"
#include "softdisplaypolicy.h"
#include "biltoo_thread.h"

#include "imageloader.h"
#include "imagecache.h"
#include "contentxform.h"
#include "sessionappearance.h"
#include "coloradjust.h"
#include "imagesizebook.h"
#include "biltoo_logging.h"
#include "ttfp_trace.h"
#include "viewtransform.h"

#include <QFileInfo>
#include <QThreadPool>
#include <QPointer>
#include <QMetaObject>
#include <QTimer>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QVarLengthArray>
#include <cstdlib>
#include <cstdio>

WorkspaceItemState DisplayPipelineController::appearanceForNewImageModeItem(const QString &path)
{
    // Prefer stable session-image id appearance; path map is legacy only.
    //
    // Image mode LoadReplace: the sole canvas item is the current session
    // image, so m_view->hostSessionId().currentIdValue() identifies it correctly.
    //
    // Gallery / Workspace LoadAdd must not call this: each tile is bound to
    // its own session id *after* creation. Applying m_view->hostSessionId().currentIdValue() here
    // would bake the navigated image's crop into every newly decoded tile.
    if (m_view->hostSessionId().hasCurrentId()) {
        const SessionImageId curId = m_view->hostSessionId().currentIdValue();
        seedSessionAppearanceFromState(curId, path);
        if (m_view->itemWorld().hasDurableAppearance(curId)) {
            return m_view->sessionAppearanceValue(curId);
        }
        // Bound session image with no appearance entry = full frame, no path fallback.
        return {};
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(path)) {
        return *st;
    }
    return {};
}


ImageItem *DisplayPipelineController::createItemFromImage(const QString &path, const QImage &image,
                                          bool applyStoredSessionCrop)
{
    if (image.isNull()) {
        return nullptr;
    }
    // @p image is always host-raw (workers no longer bake). Size-first ctor;
    // never QPixmap::fromImage of multi-MP in ImageItem(path, image).
    WorkspaceItemState app;
    if (applyStoredSessionCrop && m_view->isImageMode()) {
        // Always attempt seed from path XDG when bound. The old gate
        // !(haveId && !m_view->itemWorld().getAppearance(id)) *skipped* seed when the slot was
        // empty — which is exactly when durable rotate/flip must be loaded
        // after restart. appearanceForNewImageModeItem seeds then returns
        // identity only if XDG has nothing.
        if (m_view->hostSessionId().hasCurrentId()
            || m_view->itemWorld().pathBook().contains(path)) {
            app = appearanceForNewImageModeItem(path);
        }
    }
    // Logical size only from probe / map — never sample (LQIP/soft) dims.
    QSize native = m_view->layoutSizeForPath(path, QImage());
    if (m_view->hostSizeBook().isProvisional(path)
        || !isPositiveSize(native) || native.width() <= 1 || native.height() <= 1) {
        // Cold: 1×1 until sizeReady; soft install must not invent geometry.
        native = QSize(1, 1);
        m_view->scheduleImageSizeProbe(path);
    }
    QSize intrinsic = ContentXform::layoutSize(native, app);
    if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
        intrinsic = QSize(1, 1);
    }

    auto *item = new ImageItem(path, intrinsic);
    m_view->applyItemModeFlags(item);
    m_view->canvasScene()->addItem(item);
    m_view->liveItems().append(item);
    registerItemDisplaySurface(item);

    // @p image is host-raw. Sole materialize site is installDisplayPixels.
    const SessionImageId sidEarly = m_view->isImageMode()
        ? m_view->hostSessionId().currentIdValue()
        : kInvalidSessionImageId;
    const ColorAdjustments storeGrade = (sidEarly != kInvalidSessionImageId)
        ? m_view->itemWorld().color(sidEarly).grade
        : app.colorAdjust;
    const bool wantBake = SessionAppearance::hasContentAppearance(app)
        || !storeGrade.isIdentity()
        || (sidEarly != kInvalidSessionImageId && m_view->itemWorld().hasColor(sidEarly));
    if (wantBake) {
        // Seed item chrome so wantAppearanceForItem can merge if the store slot
        // is still empty (bound id with no entry yet).
        m_view->syncLiveContentMetaFromState(item, app);
        m_view->syncLiveColorFromState(item, storeGrade);
        const SessionImageId sid = sidEarly;
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
        const SessionImageId sid = m_view->isImageMode()
            ? m_view->hostSessionId().currentIdValue()
            : item->sessionId();
        installDisplayPixels(item, image, kind, sid);
    }
    return item;
}



void DisplayPipelineController::seedSessionAppearancesFromPaths(const QStringList &paths,
                                                   const QVector<SessionImageId> &ids)
{
    // Fresh session: allow seed again for new ids (old set cleared on invalidate).
    const int n = ViewTransform::pairCount(paths.size(), ids.size());
    for (int i = 0; i < n; ++i) {
        m_view->hostSeedBook().clearSeedAttempted(ids.at(i));
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
    const QPointer<ImageView> guard(m_view);
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
                host->hostDisplayPipeline().markAppearanceSeedAttempted(sid);
            }
            for (const Hit &h : hits) {
                host->hostDisplayPipeline().applyStoredContentAppearanceSeed(
                    h.sid, h.path, h.stored);
            }
        }, Qt::QueuedConnection);
    });
}


void DisplayPipelineController::seedSessionAppearanceFromState(SessionImageId sid, const QString &path)
{
    if (sid == kInvalidSessionImageId || path.isEmpty()) {
        return;
    }
    // One attempt per session id — archive/miss paths must not re-hit locatorId
    // on every paint via wantAppearanceForItem.
    if (m_view->hostSeedBook().seedAttempted(sid)) {
        return;
    }
    m_view->hostSeedBook().markSeedAttempted(sid);
    ThumtooCache::StoredContentAppearance stored;
    if (!ThumtooCache::loadContentAppearance(path, &stored)) {
        return;
    }
    if (stored.isIdentity()) {
        return;
    }
    applyStoredContentAppearanceSeed(sid, path, stored);
}


void DisplayPipelineController::markAppearanceSeedAttempted(SessionImageId sid)
{
    if (sid != kInvalidSessionImageId) {
        m_view->hostSeedBook().markSeedAttempted(sid);
    }
}


void DisplayPipelineController::applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                                 const ThumtooCache::StoredContentAppearance &stored)
{
    if (sid == kInvalidSessionImageId || path.isEmpty() || stored.isIdentity()) {
        return;
    }
    // Worker path may not have marked attempted yet; mark here so paint does not
    // re-drive locatorId via wantAppearanceForItem.
    m_view->hostSeedBook().markSeedAttempted(sid);
    if (m_view->itemWorld().hasDurableAppearance(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (SessionAppearance::hasContentAppearance(m_view->sessionAppearanceValue(sid))) {
            return;
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
    m_view->itemWorld().setAppearance(sid, seed);
}


void DisplayPipelineController::installDisplayPreservingView(ImageItem *item, const QImage &pixels,
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
                  : m_view->hostSessionId().currentIdValue();
    }
    installDisplayPixels(item, pixels, kind, sid);
    m_view->preserveImageViewOnLogicalSizeChange(item, before, item->imageSize());
}


WorkspaceItemState DisplayPipelineController::wantAppearanceForItem(const ImageItem *item,
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
    if (id == kInvalidSessionImageId && m_view->isImageMode()) {
        id = m_view->hostSessionId().currentIdValue();
    }
    if (id != kInvalidSessionImageId) {
        if (m_view->itemWorld().hasDurableAppearance(id)) {
            want = m_view->sessionAppearanceValue(id);
        }
        // Cold open / ←→: id slot often empty until first seed. Path XDG holds
        // durable rotate/flip/grade — pull it before materialize or we paint
        // unoriented host forever.
        if (!SessionAppearance::hasContentAppearance(want)
            && want.colorAdjust.isIdentity()
            && !item->path().isEmpty()) {
            const_cast<DisplayPipelineController *>(this)->seedSessionAppearanceFromState(
                id, item->path());
            if (m_view->itemWorld().hasDurableAppearance(id)) {
                want = m_view->sessionAppearanceValue(id);
            }
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_view->itemWorld().getPathState(item->path())) {
            want = *st;
        }
    }
    // Phase 7: applied ContentXform is mid-edit authority (same as captureState).
    if (item->hasAppliedContentXform()) {
        item->tileContentXform().applyToState(want);
    } else if (id != kInvalidSessionImageId) {
        // No applied fingerprint: fill empty DTO fields from ItemWorld sparse
        // Crop / ContentBake (tileContentXform is applied-only after 1966).
        ContentXform::Value sparse;
        if (m_view->itemWorld().hasContentBake(id)) {
            const ItemComponents::ContentBake bake = m_view->itemWorld().contentBake(id);
            sparse.hFlip = bake.hFlip;
            sparse.vFlip = bake.vFlip;
            sparse.quarterTurns = bake.quarterTurns;
        }
        if (m_view->itemWorld().hasCrop(id)) {
            const ItemComponents::Crop crop = m_view->itemWorld().crop(id);
            sparse.hasCrop = !crop.isEmpty();
            sparse.cropRect = crop.rect;
        }
        SessionAppearance::fillEmptyContentFlags(want, sparse);
    }
    // ItemWorld Color is persistence authority for stored grade (sparse table;
    // falls back to fat DTO when the sparse entry is absent). Prefer it over a
    // stale DTO field when both exist.
    if (id != kInvalidSessionImageId) {
        want.colorAdjust = m_view->itemWorld().color(id).grade;
    }
    // Live grade leads ItemWorld during slider drag; keep store grade when live
    // is still identity (cold open / path-change before seed install).
    {
        const ColorAdjustments liveGrade = item->colorAdjustments();
        if (!liveGrade.isIdentity() || want.colorAdjust.isIdentity()) {
            want.colorAdjust = liveGrade;
        }
    }
    return want;
}

int DisplayPipelineController::itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes) const
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
    const QPointF a = m_view->mapFromScene(br.topLeft());
    const QPointF b = m_view->mapFromScene(br.bottomRight());
    const qreal longPx =
        ViewTransform::chebyshev(a, b) * m_view->devicePixelRatioF();
    return DisplayEdgePolicy::needEdgeFromScreenLongPx(longPx, allowHighRes);
}


int DisplayPipelineController::galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes) const
{
    // Visible tiles: full on-screen need. Off-screen / idle: soft band only.
    return itemOnScreenNeedEdge(item, allowHighRes);
}

void DisplayPipelineController::bindImageModeSessionCursor(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Image-mode crop/flip targets the matching Workspace session slot.
    if (m_view->hostSessionId().hasCurrentId()) {
        item->setSessionId(m_view->hostSessionId().currentIdValue());
    }
    if (m_view->hostSessionId().currentIndex() >= 0) {
        item->setSessionIndex(m_view->hostSessionId().currentIndex());
    }
}


void DisplayPipelineController::resetImageModeItemPlacement(ImageItem *item)
{
    if (!item) {
        return;
    }
    // Never inherit Gallery/Workspace free-form placement or scale.
    item->setInteractive(false);
    item->setScaleHandlesEnabled(false);
    item->applyPlacement(ItemComponents::Placement{});
}


QImage DisplayPipelineController::resolveImageModePendingPixels(const QString &path,
                                                const QImage &preview) const
{
    bool unused = false;
    return resolveImageModePendingPixels(path, preview, &unused);
}


QImage DisplayPipelineController::resolveImageModePendingPixels(const QString &path,
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
    QImage pixels = m_view->hostSlideshow().slideshowRaster(path);
    // Filmstrip often has Soft while ImageCache only has size-probe LQIP (or
    // LRU-evicted the soft). Prefer strip / shared host sample first.
    if (pixels.isNull() && m_view->hostImageModeSoftProvider()) {
        bool ready = false;
        pixels = m_view->hostImageModeSoftProvider()(path, m_view->hostSessionId().currentIdValue(), &ready);
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
    if (m_view->hostSessionId().hasCurrentId()) {
        const SessionImageId curId = m_view->hostSessionId().currentIdValue();
        if (m_view->itemWorld().hasDurableAppearance(curId)) {
            want = m_view->sessionAppearanceValue(curId);
        }
    }
    const ContentXform::Value wantX = ContentXform::Value::fromState(want);

    for (ImageItem *cand : m_view->hostGallery().stashedItems()) {
        if (!cand || cand->path() != path || !cand->hasDisplayPixels()) {
            continue;
        }
        pixels = cand->displayImage();
        if (pixels.isNull()) {
            continue;
        }
        const bool sameId = (m_view->hostSessionId().hasCurrentId()
                             && cand->sessionId() == m_view->hostSessionId().currentIdValue());
        const bool hasApplied = cand->hasAppliedContentXform();
        const ContentXform::Value applied = cand->tileContentXform();
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


SessionAppearance::PixelKind DisplayPipelineController::pixelKindForImageModeSample(
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


void DisplayPipelineController::applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path)
{
    if (!item || path.isEmpty()) {
        return;
    }
    // Content 90°/flip/crop are materialize()'d in createItemFromImage when want is set.
    // Legacy unbaked flips only if content flags not used yet.
    const WorkspaceItemState *st = m_view->itemWorld().getPathState(path);
    if (!st) {
        return;
    }
    if (!st->contentHFlip && !st->contentVFlip) {
        ItemComponents::Placement pl = item->placement();
        pl.hFlip = st->hFlip;
        pl.vFlip = st->vFlip;
        item->applyPlacement(pl);
    }
}


void DisplayPipelineController::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    if (!item) {
        return;
    }
    // Slideshow framing: when dwell motion is on, the camera sets the
    // transform (including handoff from a live transition). Applying zoom
    // framing first would centre the image then jump to motion t0.
    if (m_view->hostSlideshow().hud().isProgressActive() && m_view->hostSlideshow().settings().isMotionOff()) {
        m_view->hostSlideshow().applySlideshowZoomFraming(item);
    } else if (!m_view->hostSlideshow().hud().isProgressActive()) {
        m_view->applyImageModeFraming(item);
    }
    m_view->syncImageModeSceneRect(item);
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    m_view->hostSlideshow().maybeStartSlideshowMotion();
    if (m_view->hostSlideshow().hud().isProgressActive() && !m_view->hostSlideshow().settings().isMotionOff()
        && !m_view->hostSlideshow().dwell().isMotionActive()) {
        m_view->hostSlideshow().applySlideshowZoomFraming(item);
    }
    if (m_view->hostSlideshow().hud().isProgressActive()) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        m_view->hostSlideshow().setSlideshowPhase(path, QString(), -1.0);
    }
}


void DisplayPipelineController::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // ImageView::LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's ImageView::LoadAdd.
    if (!m_view->liveItems().isEmpty()
        || !m_view->hostBindBook().isEmpty()
        || loadGate().containsPendingWorkspacePath(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    item->setSelected(true);
    m_view->hostFraming().armFit();
    m_view->fitItem(item, m_view->currentFitAspectMode());
    emit m_view->statusChanged();
}



ImageItem *DisplayPipelineController::imageModeItemForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    ImageItem *cur = m_view->targetItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    cur = m_view->primaryItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    return nullptr;
}


QImage DisplayPipelineController::fullRasterForEdit(const QString &path) const
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


int DisplayPipelineController::imageModeOnScreenNeedEdge() const
{
    if (!m_view->isImageMode()) {
        return 0;
    }
    const ImageItem *item = m_view->targetItem();
    if (!item) {
        item = m_view->primaryItem();
    }
    return itemOnScreenNeedEdge(item, /*allowHighRes=*/true);
}


void DisplayPipelineController::onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role)
{
    // Host path→raster map: keep display-ladder samples (≤ kDisplayMaxEdge),
    // not a hard 512 preview. Slideshow must reuse Image-mode sharpness.
    if (!image.isNull() && !path.isEmpty()) {
        ImageCache::put(path, image);
        // Quality/soft climb during slideshow → phase buffer + atlas upgrade.
        if (m_view->hostSlideshow().hud().isProgressActive()) {
            m_view->hostSlideshow().onSlideshowRasterReady(path, image);
        }
    }
    switch (static_cast<ImageView::LoadRole>(role)) {
    case ImageView::LoadReplace:
        completeLoadReplace(path, image, generation);
        break;
    case ImageView::LoadRestore:
        completeLoadRestore(path, image);
        break;
    case ImageView::LoadAdd:
        completeLoadAdd(path, image, generation);
        break;
    }
}



bool DisplayPipelineController::loadImage(const QString &path)
{
    m_view->hostImage().setClassicPath(path);
    m_view->clearTextSelection();
    m_view->hostTextLayer().clearLinkHoverTip();
    if (m_view->hostTextLayer().showsRegions() || m_view->hostTextLayer().hasSearchQuery()) {
        m_view->refreshTextLayer();
    }
    m_view->hostSessionId().clearLastLoadError();

    if (m_view->isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_view->liveItems().isEmpty()) {
            scheduleImageLoad(path, ImageView::LoadReplace);
        }
        emit m_view->statusChanged();
        return true;
    }

    // Classic mode: soft from cache immediately; PreferCache climbs in background.
    // Do not emit statusChanged — setCurrentIndex chrome already updateStatus.
    scheduleImageLoad(path, ImageView::LoadReplace);
    return true;
}


void DisplayPipelineController::ensureImageFocusSurface()
{
    if (!m_view->isImageMode()) {
        if (imageFocusSurfaceRef() != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item || item->path().isEmpty()) {
        imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        return;
    }
    // Prefer the canvas item registry (create/destroy lifecycle).
    if (item->displaySurfaceId() == 0) {
        registerItemDisplaySurface(item);
    }
    // Kind may have been GalleryTile if item was created before mode switch.
    const auto itemSid =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
    const DisplaySurface::Binding *ib = displaySurfaces().binding(itemSid);
    if (ib && ib->kind != DisplaySurface::Kind::ImageFocus) {
        registerItemDisplaySurface(item); // rebind as ImageFocus
    }
    imageFocusSurfaceRef() =
        static_cast<DisplaySurface::SurfaceId>(item->displaySurfaceId());
}


void DisplayPipelineController::syncImageFocusSurfaceState()
{
    ensureImageFocusSurface();
    if (imageFocusSurfaceRef() == DisplaySurface::kInvalidSurfaceId) {
        return;
    }
    ImageItem *item = m_view->primaryItem();
    if (!item) {
        return;
    }
    const QString path = item->path();
    const bool pending =
        m_view->hostPathRaster() && !path.isEmpty() && m_view->hostPathRaster()->isClimbPending(path);
    syncItemDisplaySurface(item, -1, pending);
    if (m_view->hostCrop().session().isDraftSampleFrozen() && m_view->hostCrop().isCropDraftLockedPath(path)) {
        displaySurfaces().setFrozen(imageFocusSurfaceRef(), true);
    }
}


