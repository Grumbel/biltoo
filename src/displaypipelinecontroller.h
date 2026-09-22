// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISPLAYPIPELINECONTROLLER_H
#define DISPLAYPIPELINECONTROLLER_H

#include "sessionloadgate.h"
#include "displaysurface.h"
#include "tilelod/tile_lod_item_bag.hpp"
#include "sessionappearance.h"
#include "pathrasterservice.h"
#include "thumtoocache.h"

#include <QTimer>

#include <memory>
#include <unordered_map>

class ImageView;
class ImageItem;
class TileLoadCoordinator;

/**
 * Display / load pipeline collaborator for ImageView (Phase 6 Tier 5a).
 *
 * Owns load generation gate, display surfaces, focus surface id, tile LOD
 * timers, PreferCache climbs, sample install, and surface drive/bind (Tier 5b).
 * Implementation split: displaypipelinecontroller.cpp (climbs/surface/install),
 * _load.cpp (schedule/LoadAdd/gallery), _item.cpp (create/seed/want/frame).
 * SIZE.md soft-vs-logical rules stay with ImageSizeBook.
 */
class DisplayPipelineController
{
public:

    explicit DisplayPipelineController(ImageView *view);
    ~DisplayPipelineController();

    ImageView *view() const { return m_view; }

    SessionLoadGate &loadGate() { return m_loadGate; }
    const SessionLoadGate &loadGate() const { return m_loadGate; }

    DisplaySurfaceController &displaySurfaces() { return m_displaySurfaces; }
    const DisplaySurfaceController &displaySurfaces() const { return m_displaySurfaces; }

    DisplaySurface::SurfaceId &imageFocusSurfaceRef() { return m_imageFocusSurface; }

    std::unique_ptr<TileLoadCoordinator> &tileCoordinator() { return m_tileCoordinator; }
    const std::unique_ptr<TileLoadCoordinator> &tileCoordinator() const { return m_tileCoordinator; }

    QTimer *&tileLodTimer() { return m_tileLodTimer; }
    QTimer *tileLodTimer() const { return m_tileLodTimer; }

    QTimer *&tileLodZoomDebounce() { return m_tileLodZoomDebounce; }
    QTimer *tileLodZoomDebounce() const { return m_tileLodZoomDebounce; }

    /**
     * PreferCache / PathRaster quality climb for selected (or bounded) Workspace
     * items. Body moved from ImageView (Tier 5b); ImageView thin-forwards.
     */
    void ensureWorkspaceQualityClimb();
    void requestEscalateClimb(const QString &path, int wantEdge);
    int cappedDisplayEdgeForPath(const QString &path, int wantEdge) const;
    bool sampleCoversNativeLogical(const QString &path, const QImage &image) const;
    void onLadderReady(const QString &path, int maxEdge, const QImage &image);
    void maybeClimbImageModePixelsForView();
    void driveImageFocusSurface();
    void registerItemDisplaySurface(ImageItem *item);
    void unregisterItemDisplaySurface(ImageItem *item);
    void installDisplayPixels(ImageItem *item, const QImage &pixels,
                              SessionAppearance::PixelKind kind,
                              SessionImageId sid);
    void installImageModeReplaceItem(const QString &path, const QImage &image);
    void completeLoadReplace(const QString &path, const QImage &image, quint64 generation);
    /** @p role is ImageView::LoadRole as int (avoid circular header). */
    void scheduleImageLoad(const QString &path, int role);
    void galleryDecodeResetPath(const QString &path);
    void galleryDecodeResetAll();
    void scheduleGalleryDecode(const QString &path);

    void scheduleTileLodAfterInteraction(int delayMs = 50);
    void purgeTilePathRam(const QString &path);
    /** Drop one item's tile session if a bag exists (shared path cache kept). No ensure. */
    void dropItemTileLodSession(ImageItem *item);
    /** Ensure pipeline-owned bag for @p item; attaches to the item. */
    tilelod::ItemBag &ensureTileBag(ImageItem *item);
    /** Detach and destroy pipeline-owned bag (canvas destroy). */
    void releaseTileBag(ImageItem *item);
    /** Detach all items and clear the map (view teardown). */
    void releaseAllTileBags();
    /** Non-owning access (nullptr if never ensured). */
    tilelod::ItemBag *tileLodBag(ImageItem *item);
    const tilelod::ItemBag *tileLodBag(const ImageItem *item) const;
    /** Crop-draft freeze: suppress tile requests/paint for this item. */
    void setItemTileLodSuppressed(ImageItem *item, bool on);
    /**
     * Per-item tile service entry (prepare + pump + issue).
     * Only TileLoadCoordinator should pass budget > 0.
     */
    void tickItemTileLod(ImageItem *item, int budget = 8);
    void dropAllTileLodSessions();
    void tickPrimaryTileLod(int budget = 8);
    void onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role);
    void completeLoadRestore(const QString &path, const QImage &image);
    ImageItem *createItemFromImage(const QString &path, const QImage &image,
                                   bool applyStoredSessionCrop = true);
    void seedSessionAppearancesFromPaths(const QStringList &paths,
                                         const QVector<SessionImageId> &ids);
    void seedSessionAppearanceFromState(SessionImageId sid, const QString &path);
    WorkspaceItemState wantAppearanceForItem(const ImageItem *item,
                                             SessionImageId sid) const;
    ImageItem *createPlaceholderItem(const QString &path, const QSize &intrinsicSize);
    int galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes = false) const;
    void bindImageModeSessionCursor(ImageItem *item);
    QImage fullRasterForEdit(const QString &path) const;
    int imageModeOnScreenNeedEdge() const;
    void onImageLoaded(const QString &path, const QImage &image, quint64 generation, int role);
    bool loadImage(const QString &path);


private:
    // Load / climb / surface / install helpers (no external callers)
    /**
     * When tiles own display for @p path: tick primary tile LOD and return true
     * (PreferCache whole-frame climb should skip).
     *
     * @p countDurable when true (default): tileLodWanted **or** known durable
     * pyramid — including durable-only with no live underlay (prefetch).
     * When false: only live items with tileLodWanted (ladderReady soft
     * deliveries — avoid ticking every PreferCache underlay).
     */
    bool tickTilesIfOwnDisplay(const QString &path, bool countDurable = true);
    /**
     * Image underlay soft sources (host-raw only). @p displayReadyOut set true
     * only for rare attach-ready samples; normally false (materialize path).
     */
    QImage resolveImageModePendingPixels(const QString &path,
                                         const QImage &preview = QImage(),
                                         bool *displayReadyOut = nullptr) const;
    void scheduleImageModeNativeDecodeOnce(const QString &path);
    void ensureImageModeQualityClimb(const QString &path, const QImage &sample);
    void installImageModeSampleInPlace(ImageItem *item, const QString &path,
                                       const QImage &image,
                                       SessionAppearance::PixelKind kind);
    bool tryInstallImageModeSample(const QString &path, const QImage &image);
    bool tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                        SessionAppearance::PixelKind kind);
    void upgradeImageModeFromLadder(const QString &path, int maxEdge, const QImage &image);
    void applyGalleryLadderReady(const QString &path, int maxEdge, const QImage &image);
    void applyWorkspaceLadderReady(const QString &path, int maxEdge, const QImage &image);
    DisplaySurface::State displaySurfaceStateForItem(const ImageItem *item,
                                                     int hostLongEdge = -1,
                                                     bool climbPending = false) const;
    bool applyDisplaySurfaceAction(ImageItem *item,
                                   const DisplaySurface::Action &act,
                                   const QImage &hostSample,
                                   int fallbackNeedEdge,
                                   PathRasterService::ClimbPolicy climbPolicy);
    void syncItemDisplaySurface(ImageItem *item, int hostLongEdge = -1,
                                bool climbPending = false);
    bool canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                SessionAppearance::PixelKind kind) const;
    void installImageModePendingTile(const QString &path, const QImage &preview = QImage());
    void finishLoadAddStatus(bool refreshGalleryWindow);
    bool acceptPendingLoadAdd(const QString &path, quint64 generation);
    void handleLoadAddDecodeFailure(const QString &path);
    void fillStashedItemsForPath(const QString &path, const QImage &image);
    void reassertPendingBindPlacement(const QString &path);
    void claimUnboundItemsForPendingBinds(const QString &path, const QImage &image);
    int fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                       bool *sizeChangedOut);
    void createMissingLoadAddItems(const QString &path, const QImage &image,
                                   int have, int wanted);
    void applyLoadAddLayoutAfterMembership(bool sizeChanged);
    void completeLoadAdd(const QString &path, const QImage &image, quint64 generation);
    bool tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen);
    void scheduleSlideshowReplaceDecode(const QString &path, quint64 gen, int role);
    void scheduleClassicImageDecode(const QString &path, quint64 gen, int role);
    int galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut) const;
    bool takePendingRestoreState(const QString &path, WorkspaceItemState *out);
    WorkspaceItemState appearanceForNewImageModeItem(const QString &path);
    void markAppearanceSeedAttempted(SessionImageId sid);
    void applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                          const ThumtooCache::StoredContentAppearance &stored);
    int itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes = false) const;
    void resetImageModeItemPlacement(ImageItem *item);
    SessionAppearance::PixelKind pixelKindForImageModeSample(const QString &path,
                                                             const QImage &image) const;
    void frameImageModeReplaceItem(ImageItem *item, const QString &path);
    void seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image);
    ImageItem *imageModeItemForPath(const QString &path) const;
    void ensureImageFocusSurface();
    void syncImageFocusSurfaceState();

    /**
     * SessionImageId for materialize / layout: @p preferred if set, else
     * item->sessionId(), else Image-mode cursor id. Matches
     * ImageView::resolveContentEditSessionId (2215 / 2219).
     */
    SessionImageId resolveItemSessionId(
        const ImageItem *item,
        SessionImageId preferred = kInvalidSessionImageId) const;

    ImageView *m_view = nullptr; // not owned
    SessionLoadGate m_loadGate;
    DisplaySurfaceController m_displaySurfaces;
    DisplaySurface::SurfaceId m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;
    std::unique_ptr<TileLoadCoordinator> m_tileCoordinator;
    /** Stage 2: per-item tile LOD bags (owned here when attached). */
    std::unordered_map<ImageItem *, std::unique_ptr<tilelod::ItemBag>> m_tileBags;
    QTimer *m_tileLodTimer = nullptr;
    QTimer *m_tileLodZoomDebounce = nullptr;
};

#endif // DISPLAYPIPELINECONTROLLER_H
