// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "display/displaypipelinecontroller.h"
#include "item/itemcomponents.h"
#include "display/displaypipeline_jobs.h"

#include "display/tile_load_coordinator.h"

#include "imageview.h"
#include "imageitem.h"
#include "display/displayedgepolicy.h"
#include "display/pathrasterservice.h"
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include "gallery/gallerydecodesm.h"
#include "display/displayquality.h"
#include "display/lqipdisplaypolicy.h"
#include "util/biltoo_thread.h"

#include "host/imageloader.h"
#include "display/imagecache.h"
#include "content/contentxform.h"
#include "session/sessionappearance.h"
#include "color/coloradjust.h"
#include "item/imagesizebook.h"
#include "util/biltoo_logging.h"
#include "util/ttfp_trace.h"
#include "view/viewtransform.h"

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
    // image, so m_host->hostSessionId().currentIdValue() identifies it correctly.
    //
    // Gallery / Workspace LoadAdd must not call this: each tile is bound to
    // its own session id *after* creation. Applying m_host->hostSessionId().currentIdValue() here
    // would bake the navigated image's crop into every newly decoded tile.
    if (m_host->hostSessionId().hasCurrentId()) {
        SessionImageId curId = m_host->hostSessionId().currentIdValue();
        // Align id with path before any path-XDG seed (seed writes into ItemWorld
        // under sid — wrong pairing poisons contentBake for the other row).
        if (SessionDocument *doc = m_host->sessionDocument()) {
            const int docIdx = doc->indexOfId(curId);
            if (docIdx >= 0 && !path.isEmpty() && doc->paths().at(docIdx) != path) {
                const int byPath = doc->indexOfPathPreferId(path);
                if (byPath >= 0) {
                    curId = doc->idAt(byPath);
                } else {
                    curId = kInvalidSessionImageId;
                }
            }
        }
        if (curId != kInvalidSessionImageId) {
            // Image underlay create: ItemWorld only — never path-XDG seed here.
            // Session open (seedSessionAppearancesFromPaths) and Gallery already
            // seed; installDisplayPixels also refuses XDG (ECS_GUI_BYPASSES #7).
            if (m_host->itemWorld().hasDurableAppearance(curId)) {
                return m_host->sessionAppearanceValue(curId);
            }
            // Bound with empty ItemWorld = full frame, no path fallback.
            return {};
        }
    }
    // Path map only when unbound (no session image id).
    if (const WorkspaceItemState *st = m_host->itemWorld().getPathState(path)) {
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
    if (applyStoredSessionCrop && m_host->isImageMode()) {
        // ItemWorld only (2205 — no path-XDG seed on Image underlay create).
        if (m_host->hostSessionId().hasCurrentId()
            || m_host->itemWorld().pathBook().contains(path)) {
            app = appearanceForNewImageModeItem(path);
        }
        // Match installDisplayPixels: placement-only durable row is not content
        // orient — layout must not transpose while paint stays identity.
        const SessionImageId sidLayout = m_host->hostSessionId().currentIdValue();
        if (sidLayout != kInvalidSessionImageId) {
            app = SessionAppearance::orientAuthorityWant(
                m_host->itemWorld().hasContentOrient(sidLayout), app);
        }
    }
    // Logical size only from probe / map — never sample (LQIP/soft) dims.
    QSize native = m_host->layoutSizeForPath(path, QImage());
    if (m_host->hostSizeBook().isProvisional(path)
        || !isPositiveSize(native) || native.width() <= 1 || native.height() <= 1) {
        // Cold: 1×1 until sizeReady; soft install must not invent geometry.
        native = QSize(1, 1);
        m_host->scheduleImageSizeProbe(path);
    }
    QSize intrinsic = ContentXform::layoutSize(native, app);
    if (!(intrinsic.width() > 1 && intrinsic.height() > 1)) {
        intrinsic = QSize(1, 1);
    }

    auto *item = new ImageItem(path, intrinsic);
    m_host->applyItemModeFlags(item);
    m_host->canvasScene()->addItem(item);
    m_host->liveItems().append(item);
    registerItemDisplaySurface(item);

    // @p image is host-raw. Sole materialize site is installDisplayPixels.
    const SessionImageId sidEarly = m_host->isImageMode()
        ? m_host->hostSessionId().currentIdValue()
        : kInvalidSessionImageId;
    const ColorAdjustments storeGrade = (sidEarly != kInvalidSessionImageId)
        ? m_host->itemWorld().color(sidEarly).grade
        : app.colorAdjust;
    const bool wantBake = SessionAppearance::hasContentAppearance(app)
        || !storeGrade.isIdentity()
        || (sidEarly != kInvalidSessionImageId && m_host->itemWorld().hasColor(sidEarly));
    if (wantBake) {
        // Seed item chrome so wantAppearanceForItem can merge if the store slot
        // is still empty (bound id with no entry yet).
        m_host->syncLiveContentMetaFromState(item, app);
        m_host->syncLiveColorFromState(item, storeGrade);
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
        const SessionImageId sid = m_host->isImageMode()
            ? m_host->hostSessionId().currentIdValue()
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
        m_host->hostSeedBook().clearSeedAttempted(ids.at(i));
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
    // Never write path-keyed XDG orient into a SessionImageId that the document
    // binds to a different path (wrong id + seed = permanent contentBake leak).
    if (SessionDocument *doc = m_host->sessionDocument()) {
        const int docIdx = doc->indexOfId(sid);
        if (docIdx >= 0 && doc->paths().at(docIdx) != path) {
            qCritical("seedSessionAppearanceFromState: refuse sid=%lld path=%s "
                      "(document path is %s)",
                      static_cast<long long>(sid),
                      qPrintable(path),
                      qPrintable(doc->paths().at(docIdx)));
            return;
        }
    }
    // One attempt per session id — archive/miss paths must not re-hit locatorId
    // on every paint via wantAppearanceForItem.
    if (m_host->hostSeedBook().seedAttempted(sid)) {
        return;
    }
    m_host->hostSeedBook().markSeedAttempted(sid);
    // Store loadContentAppearance is SQLite — never on the GUI.
    const QPointer<ImageView> guard(m_view);
    const SessionImageId sidCopy = sid;
    const QString pathCopy = path;
    QThreadPool::globalInstance()->start([guard, sidCopy, pathCopy]() {
        ASSERT_NOT_GUI_THREAD();
        ThumtooCache::StoredContentAppearance stored;
        if (!ThumtooCache::loadContentAppearance(pathCopy, &stored)
            || stored.isIdentity()) {
            return;
        }
        QMetaObject::invokeMethod(guard.data(), [guard, sidCopy, pathCopy, stored]() {
            ImageView *host = guard.data();
            if (!host) {
                return;
            }
            host->hostDisplayPipeline().applyStoredContentAppearanceSeed(
                sidCopy, pathCopy, stored);
        }, Qt::QueuedConnection);
    });
}


void DisplayPipelineController::markAppearanceSeedAttempted(SessionImageId sid)
{
    if (sid != kInvalidSessionImageId) {
        m_host->hostSeedBook().markSeedAttempted(sid);
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
    m_host->hostSeedBook().markSeedAttempted(sid);
    if (m_host->itemWorld().hasDurableAppearance(sid)) {
        // Keep a non-identity entry; refill only if the slot is still empty of
        // content ops so Gallery→Image cannot miss durable orientation.
        if (SessionAppearance::hasContentAppearance(m_host->sessionAppearanceValue(sid))) {
            return;
        }
    }
    WorkspaceItemState seed;
    seed.sessionId = sid;
    seed.path = path;
    // Orient/flip/grade only. Crop is per SessionImageId — never seed from
    // path-keyed XDG (duplicates share a path; last crop would leak).
    // Orient/grade only — never path-XDG crop (duplicates share a path).
    SessionAppearance::applyStoredContentAppearance(&seed, stored, true, false);
    // Upsert orient/grade only — must not clear attention or Placement.
    m_host->itemWorld().mergeContentFromState(sid, seed);
}


SessionImageId DisplayPipelineController::resolveItemSessionId(
    const ImageItem *item, SessionImageId preferred) const
{
    if (preferred != kInvalidSessionImageId) {
        return preferred;
    }
    if (!item) {
        return kInvalidSessionImageId;
    }
    if (item->sessionId() != kInvalidSessionImageId) {
        return item->sessionId();
    }
    if (m_host->isImageMode()) {
        return m_host->hostSessionId().currentIdValue();
    }
    return kInvalidSessionImageId;
}


WorkspaceItemState DisplayPipelineController::wantAppearanceForItem(const ImageItem *item,
                                                      SessionImageId sid) const
{
    WorkspaceItemState want;
    if (!item) {
        return want;
    }
    const SessionImageId id = resolveItemSessionId(item, sid);
    if (id != kInvalidSessionImageId) {
        if (m_host->itemWorld().hasDurableAppearance(id)) {
            want = m_host->sessionAppearanceValue(id);
        }
        // Gallery/Workspace: path XDG seed when id slot is empty (cold pack).
        // Image mode: never seed here — session open seeds; underlay install
        // zeros orient without contentBake (Workspace Placement vs Image orient).
        if (!m_host->isImageMode()
            && !SessionAppearance::hasContentAppearance(want)
            && want.colorAdjust.isIdentity()
            && !item->path().isEmpty()) {
            const_cast<DisplayPipelineController *>(this)->seedSessionAppearanceFromState(
                id, item->path());
            if (m_host->itemWorld().hasDurableAppearance(id)) {
                want = m_host->sessionAppearanceValue(id);
            }
        }
    } else if (item->sessionId() == kInvalidSessionImageId) {
        if (const WorkspaceItemState *st = m_host->itemWorld().getPathState(item->path())) {
            want = *st;
        }
    }
    // Mid-edit authority is applied on *this* live ImageItem only — never the
    // ItemWorld residual applied table (that outlived Workspace→Image and overrode
    // sparse contentBake). Durable ground truth is contentBake/crop sparse.
    if (item->hasAppliedContentXform()) {
        item->tileContentXform().applyToState(want);
    } else if (id != kInvalidSessionImageId) {
        // Fill empty DTO fields from ItemWorld sparse contentBake/crop.
        ContentXform::Value sparse;
        if (m_host->itemWorld().hasContentBake(id)) {
            const ItemComponents::ContentBake bake = m_host->itemWorld().contentBake(id);
            sparse.hFlip = bake.hFlip;
            sparse.vFlip = bake.vFlip;
            sparse.quarterTurns = bake.quarterTurns;
        }
        if (m_host->itemWorld().hasCrop(id)) {
            const ItemComponents::Crop crop = m_host->itemWorld().crop(id);
            sparse.hasCrop = !crop.isEmpty();
            sparse.cropRect = crop.rect;
        }
        SessionAppearance::fillEmptyContentFlags(want, sparse);
        // Placement/color-only durable row is not content orient (2205–2211).
        want = SessionAppearance::orientAuthorityWant(
            m_host->itemWorld().hasContentOrient(id), want);
    }
    // ItemWorld Color is persistence authority for stored grade (sparse table;
    // sparse tables only). Prefer it over a
    // stale DTO field when both exist.
    if (id != kInvalidSessionImageId) {
        want.colorAdjust = m_host->itemWorld().color(id).grade;
    }
    // Live grade leads ItemWorld during slider drag; keep store grade when live
    // is still identity (cold open / path-change before seed install).
    {
        const ColorAdjustments liveGrade = m_host->itemLiveColor(item);
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
    const QPointF a = m_host->mapFromScene(br.topLeft());
    const QPointF b = m_host->mapFromScene(br.bottomRight());
    const qreal longPx =
        ViewTransform::chebyshev(a, b) * m_host->devicePixelRatioF();
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
    // If hostSessionId points at a different document path than this underlay,
    // resolve the id for the underlay path (do not leave unbound or bind wrong).
    if (m_host->hostSessionId().hasCurrentId()) {
        SessionImageId sid = m_host->hostSessionId().currentIdValue();
        int listIdx = m_host->hostSessionId().currentIndex();
        if (SessionDocument *doc = m_host->sessionDocument()) {
            const int docIdx = doc->indexOfId(sid);
            if (docIdx >= 0 && !item->path().isEmpty()
                && doc->paths().at(docIdx) != item->path()) {
                qCritical("bindImageModeSessionCursor: fixup sid=%lld for path=%s "
                          "(was document path %s)",
                          static_cast<long long>(sid),
                          qPrintable(item->path()),
                          qPrintable(doc->paths().at(docIdx)));
                const int byPath = doc->indexOfPathPreferId(item->path());
                if (byPath >= 0) {
                    sid = doc->idAt(byPath);
                    listIdx = byPath;
                    m_host->setCurrentSessionId(sid);
                } else {
                    sid = kInvalidSessionImageId;
                }
            }
        }
        if (sid != kInvalidSessionImageId) {
            m_host->setItemSessionId(item, sid);
            if (m_host->sessionListIndex(item) < 0 && listIdx >= 0) {
                item->setSessionIndex(listIdx);
            }
        } else if (listIdx >= 0) {
            item->setSessionIndex(listIdx);
        }
    } else if (m_host->hostSessionId().currentIndex() >= 0) {
        item->setSessionIndex(m_host->hostSessionId().currentIndex());
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
                                                const QImage &preview,
                                                bool *displayReadyOut) const
{
    // Image-mode underlay soft — independent of previous ViewMode.
    // Workspace→Image and Gallery→Image must share this path (MODE_OWNERSHIP /
    // structural). Mode-stash display samples are NOT sources: they may already
    // be content-baked and disagreed with ItemWorld or host-raw cache (random
    // double/wrong orient when opening from Workspace).
    //
    // Sources (process memory only, host-raw):
    // 1) Explicit preview (ladder soft)
    // 2) Slideshow raster (ImageCache host)
    // 3) Filmstrip host soft (ready=false only — 2204 never returns id override)
    // 4) ImageCache / LQIP
    // Orient always from ItemWorld via installDisplayPixels materialize.
    if (displayReadyOut) {
        *displayReadyOut = false;
    }
    if (!preview.isNull()) {
        return preview;
    }
    QImage pixels = m_host->hostSlideshow().slideshowRaster(path);
    if (!pixels.isNull()) {
        return pixels;
    }

    if (m_host->hostImageModeSoftProvider()) {
        bool ready = false;
        pixels = m_host->hostImageModeSoftProvider()(
            path, m_host->hostSessionId().currentIdValue(), &ready);
        // ready=true would be content-baked override — discard (ECS #1/#2).
        if (!pixels.isNull() && !ready) {
            return pixels;
        }
        pixels = QImage();
    }
    pixels = ImageCache::get(path);
    if (pixels.isNull()) {
        pixels = ThumtooCache::cachedLqipImage(path);
        if (!pixels.isNull()) {
            ImageCache::put(path, pixels);
        }
    }
    return pixels;
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


void DisplayPipelineController::frameImageModeReplaceItem(ImageItem *item, const QString &path)
{
    if (!item) {
        return;
    }
    // Slideshow framing: when dwell motion is on, the camera sets the
    // transform (including handoff from a live transition). Applying zoom
    // framing first would centre the image then jump to motion t0.
    if (m_host->hostSlideshow().hud().isProgressActive() && m_host->hostSlideshow().settings().isMotionOff()) {
        m_host->hostSlideshow().applySlideshowZoomFraming(item);
    } else if (!m_host->hostSlideshow().hud().isProgressActive()) {
        m_host->applyImageModeFraming(item);
    }
    m_host->syncImageModeSceneRect(item);
    // Apply camera while updates are still blocked and any live hold still
    // covers the viewport — avoids a flash of identity / wrong pan pose.
    m_host->hostSlideshow().maybeStartSlideshowMotion();
    if (m_host->hostSlideshow().hud().isProgressActive() && !m_host->hostSlideshow().settings().isMotionOff()
        && !m_host->hostSlideshow().dwell().isMotionActive()) {
        m_host->hostSlideshow().applySlideshowZoomFraming(item);
    }
    if (m_host->hostSlideshow().hud().isProgressActive()) {
        item->setVisible(false);
        // Paused ←/→ loads the underlay while pure phase still paints the
        // previous path — refresh dwell to this decode.
        m_host->hostSlideshow().setSlideshowPhase(path, QString(), -1.0);
    }
}


void DisplayPipelineController::seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image)
{
    // Workspace with empty canvas: seed with navigated image — only for
    // genuine session navigation. Project load / membership adds schedule
    // ImageView::LoadAdd with pending binds; seeding first would leave an unbound tile
    // (default placement, no flip/grade) and steal the first path's ImageView::LoadAdd.
    if (!m_host->liveItems().isEmpty()
        || !m_host->hostBindBook().isEmpty()
        || loadGate().containsPendingWorkspacePath(path)) {
        return;
    }
    ImageItem *item = createItemFromImage(path, image);
    if (!item) {
        return;
    }
    // Bind the navigated session image so the seed tile is never unbound.
    // LoadReplace on an empty multi-item canvas is session navigation, not an
    // ad-hoc path place — hostSessionId is the cursor identity.
    if (m_host->hostSessionId().hasCurrentId()) {
        const SessionImageId sid = m_host->hostSessionId().currentIdValue();
        m_host->setItemSessionId(item, sid);
        if (m_host->sessionListIndex(item) < 0
            && m_host->hostSessionId().currentIndex() >= 0) {
            item->setSessionIndex(m_host->hostSessionId().currentIndex());
        }
        if (m_host->itemWorld().hasDurableAppearance(sid)) {
            m_host->applyState(item, m_host->sessionAppearanceValue(sid));
        }
    } else if (m_host->hostSessionId().currentIndex() >= 0) {
        item->setSessionIndex(m_host->hostSessionId().currentIndex());
    }
    item->setSelected(true);
    m_host->hostFraming().armFit();
    m_host->fitItem(item, m_host->currentFitAspectMode());
    m_host->notifyStatusChanged();
}



ImageItem *DisplayPipelineController::imageModeItemForPath(const QString &path) const
{
    if (path.isEmpty()) {
        return nullptr;
    }
    ImageItem *cur = m_host->targetItem();
    if (cur && cur->path() == path) {
        return cur;
    }
    cur = m_host->primaryItem();
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
    if (!m_host->isImageMode()) {
        return 0;
    }
    const ImageItem *item = m_host->targetItem();
    if (!item) {
        item = m_host->primaryItem();
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
        if (m_host->hostSlideshow().hud().isProgressActive()) {
            m_host->hostSlideshow().onSlideshowRasterReady(path, image);
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
    m_host->hostImage().setClassicPath(path);
    m_host->clearTextSelection();
    m_host->hostTextLayer().clearLinkHoverTip();
    if (m_host->hostTextLayer().showsRegions() || m_host->hostTextLayer().hasSearchQuery()) {
        m_host->refreshTextLayer();
    }
    m_host->hostSessionId().clearLastLoadError();

    if (m_host->isMultiItemMode()) {
        // Session navigation while in multi-item mode does not destroy the canvas;
        // only ensure the path is available as classic fallback.
        // Still show the navigated image if the workspace is empty.
        if (m_host->liveItems().isEmpty()) {
            scheduleImageLoad(path, ImageView::LoadReplace);
        }
        m_host->notifyStatusChanged();
        return true;
    }

    // Classic mode: soft from cache immediately; PreferCache climbs in background.
    // Do not emit statusChanged — setCurrentIndex chrome already updateStatus.
    scheduleImageLoad(path, ImageView::LoadReplace);
    return true;
}


void DisplayPipelineController::ensureImageFocusSurface()
{
    if (!m_host->isImageMode()) {
        if (imageFocusSurfaceRef() != DisplaySurface::kInvalidSurfaceId) {
            // Do not unbind item-owned surface; only clear the focus alias.
            imageFocusSurfaceRef() = DisplaySurface::kInvalidSurfaceId;
        }
        return;
    }
    ImageItem *item = m_host->primaryItem();
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
    ImageItem *item = m_host->primaryItem();
    if (!item) {
        return;
    }
    const QString path = item->path();
    const bool pending =
        m_host->hostPathRaster() && !path.isEmpty() && m_host->hostPathRaster()->isClimbPending(path);
    syncItemDisplaySurface(item, -1, pending);
    if (m_host->hostCrop().session().isDraftSampleFrozen() && m_host->hostCrop().isCropDraftLockedPath(path)) {
        displaySurfaces().setFrozen(imageFocusSurfaceRef(), true);
    }
}


