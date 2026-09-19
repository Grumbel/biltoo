// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include "imageview_types.h"
#include "gallerysizeresolve.h"
#include "tileneighborprefetch.h"
#include "cropsession.h"
#include "cropgeometry.h"
#include "cropflash.h"
#include "attentionsession.h"
#include "centreprogress.h"
#include "grouptransformsession.h"
#include "pageguidesession.h"
#include "iteminteractsession.h"
#include "hudflash.h"
#include "viewframing.h"
#include "textlayersession.h"
#include "zoomregiongesture.h"
#include "canvasbackground.h"
#include "layoutprefs.h"
#include "viewportchrome.h"
#include "hudappearance.h"
#include "sessionchrome.h"
#include "gallerysoftbook.h"
#include "perfstats.h"
#include "coloradjustcommit.h"
#include "layoutdebounce.h"
#include "galleryrelayoutsuppress.h"
#include "layoutapplyguard.h"
#include "imagesizebook.h"
#include "pathitemstatebook.h"
#include "pendingitemappearancebook.h"
#include "slideshowtypes.h"
#include "motionscrollchrome.h"
#include "loadgeneration.h"
#include "sessionloadgate.h"
#include "sessionbindbook.h"
#include "sessionpathorder.h"
#include "thumtoocache.h"
#include "coloradjust.h"
#include "sessionappearance.h"
#include "gallerycontroller.h"
#include "workspacecontroller.h"
#include "imagecontroller.h"
#include "pathrasterservice.h"
#include "tile_load_coordinator.h"
#include "displaysurface.h"
#include "gallerylayout.h"

#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QHash>
#include <memory>
#include <vector>

namespace tilelod { class TileLodController; }

#include <functional>
#include <QVector>
#include <QList>
#include <QImage>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QPolygonF>
#include <QUrl>

#include <atomic>

class ImageItem;
class QGraphicsScene;
class QTimer;
class QVariantAnimation;
class QUndoStack;
class QPrinter;
class QPainter;

/**
 * Central image area with three presentation modes:
 *
 * - Image: single centred non-interactive image; edge nav; slideshow.
 * - Gallery: session arranged by a packaged layout (masonry, grid, …);
 *   items are not freely moved; click opens Image mode for that file.
 * - Workspace: free-form multi-image canvas (move/scale/rotate/opacity/z).
 *
 * Gallery is not a Workspace layout — it is a separate mode of this view.
 */
class CropAppearanceCommand;

class ImageView : public QGraphicsView,
                  private GallerySizeResolveHost,
                  private TileNeighborPrefetchHost
{
    friend class CropAppearanceCommand;

    Q_OBJECT


public:
    using Tool = ::Tool;

    // BackgroundPattern: canvasbackground.h
    // SlideshowTransition / Motion / Zoom / Letterbox: slideshowtypes.h
    enum class ViewMode {
        Image,
        Gallery,
        Workspace
    };

    /** Hover / hit zone on viewport edges in Image mode. */
    enum class EdgeZone {
        None,
        Previous,
        Next,
        /** Top edge: return to Gallery or Workspace when Image was opened from there. */
        GalleryReturn
    };

    static bool isNavEdge(EdgeZone zone)
    {
        return zone == EdgeZone::Previous || zone == EdgeZone::Next
            || zone == EdgeZone::GalleryReturn;
    }

    bool hasHoverNavEdge() const { return isNavEdge(m_hoverEdge); }

    /**
     * Arrangement of items. FreeForm is used only in Workspace mode.
     * Other values are Gallery layouts.
     * Declared early so transition APIs (returnToGalleryFromImage, enterGallery)
     * can take LayoutMode before the layout methods section.
     */
    // LayoutMode: imageview_types.h

    explicit ImageView(QWidget *parent = nullptr);

    /**
     * Optional filmstrip soft source for ←/→ pending tile. When set, prefer
     * strip samples (and ImageCache) over size-probe LQIP alone.
     */
    using ImageModeSoftProvider =
        std::function<QImage(const QString &path, SessionImageId sid, bool *displayReady)>;
    void setImageModeSoftProvider(ImageModeSoftProvider provider);

    ~ImageView() override;

    bool loadImage(const QString &path);
    bool addImage(const QString &path);
    /** Add (or select) the canvas instance bound to @p sessionIndex. */
    bool addImageForSession(const QString &path, int sessionIndex);
    bool addImageForSession(const QString &path, SessionImageId sessionId, int sessionIndex);
    /** Add image and place its centre at scenePos once loaded. */
    bool addImageAt(const QString &path, const QPointF &scenePos);
    /**
     * Workspace: place @p path at @p scenePos. If the path is already on the
     * canvas, a new instance is created (duplicate) — the original is not moved.
     */
    bool placeOrMoveImageAt(const QString &path, const QPointF &scenePos);
    bool placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                            SessionImageId sessionId, int sessionIndex);
    /** Session slots currently represented on the Workspace canvas. */
    QSet<int> workspaceSessionIndices() const;
    QList<int> selectedSessionIndices() const;
    void selectBySessionIndices(const QList<int> &indices);
    /** SessionImageIds of selected canvas items (skips unbound). */
    QList<SessionImageId> selectedSessionIds() const;
    void selectBySessionIds(const QList<SessionImageId> &ids);
    /** Select live tiles matching @p paths by occurrence order (duplicate-safe). */
    void selectPathsByOccurrence(const QStringList &paths);
    /**
     * Assign sessionIndex from @p sessionFiles order (legacy). Prefer
     * rebindWorkspaceSession with stable ids.
     * Matches by path occurrence so duplicates and older unbound items work.
     */
    void rebindWorkspaceSessionIndices(const QStringList &sessionFiles);
    void rebindWorkspaceSession(const QStringList &sessionFiles,
                                const QVector<SessionImageId> &sessionIds);
    void clearWorkspace();
    /** qCritical if two live/stashed items share one SessionImageId. */
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;
    // =====================================================================
    // Mode-controller host API
    // Used by ImageController / GalleryController / WorkspaceController.
    // Prefer these over reaching into ImageView internals.
    // =====================================================================
    /** Clear drag/group/rotate interaction pointers (items stay on canvas). */
    void clearInteractionState();
    /** Controller host: stop layout debounce and clear applyingLayout. */
    void stopDeferredPacking();
    /** Controller host: drop open-time Gallery size-resolve gate (mode leave). */
    void cancelGallerySizeResolve();
    /**
     * Centre viewport progress (archive expand, size resolve, sort probes).
     * Suppresses the empty-session invite while set. Cleared with clearCentreProgress().
     */
    void setCentreProgress(const QString &title, const QString &detail = QString());
    void clearCentreProgress();
    bool hasCentreProgress() const { return m_centreProgress.active(); }
    bool gallerySizeResolveActive() const { return m_gallerySizeResolve.active(); }
    /** Controller host: set m_viewMode + m_layout.currentMode() and refresh viewport. */
    void setActiveMode(ViewMode mode, LayoutMode layout);
    /** Controller host: classic path owned by ImageController. */
    QString classicPath() const { return m_image.classicPath(); }
    bool hasClassicPath() const { return m_image.hasClassicPath(); }
    void setClassicPath(const QString &path) { m_image.setClassicPath(path); }
    void clearClassicPath() { m_image.clearClassicPath(); }
    QString takeClassicPath() { return m_image.takeClassicPath(); }
    /** Controller host: drop in-flight workspace/gallery pending load queues. */
    void clearPendingLoads();
    /** Cancel in-flight Gallery window decodes (mode leave / empty Workspace). */
    void invalidateGalleryDecodes();
    /** Open/History session barrier: bump gen, clear canvas, cancel thumtoo. */
    void invalidateSessionLoads();
    /** Controller host: scene->clear with signals blocked (stashes already detached). */
    void clearSceneKeepingStashes();
    /** Controller host: LoadReplace for a path (no-op if empty). */
    void scheduleReplaceLoad(const QString &path);
    /** Controller host: live canvas item list. */
    QList<ImageItem *> &liveItems() { return m_items; }
    const QList<ImageItem *> &liveItems() const { return m_items; }
    QGraphicsScene *canvasScene() { return m_scene; }
    /** Controller host: applyItemModeFlags to every live item. */
    void applyModeFlagsToLiveItems();
    /** Controller host: if live items exist and none selected, select first. */
    void ensurePrimarySelection();
    /** Controller host: Workspace/Gallery rubber-band vs pan drag mode from tool. */
    void applyToolDragMode();
    bool prepareSlideshowMotionDwell(ImageItem *item);
    void freezeScrollbarsForMotion();
    void resetItemPlacementForMotion(ImageItem *item);
    void armMotionBiasForPath(ImageItem *item, const QString &path);
    void startSlideshowMotion(int durationMs, qreal initialProgress = 0.0);
    /** Interval edit: keep motion progress + atlas, change duration only. */
    void retargetSlideshowMotionDuration(int durationMs);
    bool tryApplyAttentionMotionBiases(uint seed, const QImage &source);
    void applyGeometricMotionBiases(uint seed);
    void pickInterestingMotionBiases(uint seed, const QImage &source = QImage());
    /** Decode path off the GUI thread into the unified slideshow raster map. */
    void preloadSlideshowImage(const QString &path);
    void finishSlideshowPreload(const QString &path, const QImage &image);
    void pumpSlideshowPreloadQueue();
    /**
     * PathRaster Soft→PreferCache→Full (ClimbPolicy::EscalateToFull).
     * Safe to invoke via QueuedConnection from pool workers (contract §1).
     */
    void requestEscalateClimb(const QString &path, int wantEdge = 0);
    /**
     * Slideshow decode / atlas target long-edge: viewport × DPR × motion
     * headroom (Ken Burns can zoom past 1:1 cover), ladder-snapped, capped
     * at kImageLadderEdge. Not native max.
     */
    int slideshowTargetEdge() const;
    /**
     * Logical image size for @a path (never soft-raster dimensions).
     * Lookup only: m_sizeBook, then thumtoo cache. Empty if unknown.
     * Slideshow and Image-mode framing share this.
     */
    QSize logicalSizeForPath(const QString &path) const override;
    /** @deprecated name — use logicalSizeForPath. */
    QSize slideshowLogicalSize(const QString &path) const {
        return logicalSizeForPath(path);
    }
    /**
     * Ensure logical size is known (may schedule async probe).
     * Phase entry, fit, and framing call this before geometry.
     */
    QSize ensureLogicalSizeForPath(const QString &path);
    /** @deprecated name — use ensureLogicalSizeForPath. */
    QSize ensureSlideshowLogicalSize(const QString &path) {
        return ensureLogicalSizeForPath(path);
    }
    /**
     * Image→viewport scale for current slideshowZoom (Fit/Fill/Actual)
     * given logical size and viewport. Pure function of size model.
     */
    qreal slideshowZoomBaseScale(const QSize &logical, int vw, int vh) const;
    /** ≥1: panZoomFactor or pan-scan margin so zoomed frames stay sharp. */
    qreal slideshowMotionHeadroom() const;
    /**
     * User is rapidly flipping (←/→ key-repeat) in Image mode or slideshow.
     * Suppresses PreferCache climb, sync repaint, new ZoomBlur builds, and
     * atlas work until settle; previous underlay is kept until replacement.
     */
    void setSlideshowNavHot(bool hot);
    bool slideshowNavHot() const { return m_ssHud.isNavHot(); }
    /** Slideshow pure-phase owns viewport — tile coordinator must not issue. */
    bool isSlideshowProgressActive() const { return m_ssHud.isProgressActive(); }
    /** PathRasterService for PreferCache cancel when tiles issue (coordinator). */
    PathRasterService *pathRasterForCoordinator() { return m_pathRaster; }
    /** Crop draft owns the live sample — no ladder/install/rematerialize. */
    bool isCropDraftLockedItem(const ImageItem *item) const;
    bool isCropDraftLockedPath(const QString &path) const;
    /**
     * Warm overview tiles into the process-wide path registry for off-canvas
     * paths (Image-mode ±1 neighbors after nav settle). Controllers stay alive
     * and are pumped on a short timer until coverage or a tick budget expires;
     * release then leaves Succeeded tiles in the registry (1212 retain).
     * No-op while nav-hot; schedules tile pyramid when durable unknown.
     */
    void prefetchTilesForPaths(const QStringList &paths, int budgetPerPath = 4);
    /** Drop neighbor-prefetch slot for one path (session remove). */
    void dropTilePrefetchPath(const QString &path);
    /**
     * Drop tile sessions on every live item for @p path, purge registry RAM,
     * and cancel neighbor prefetch. Use on Reload / file-replaced.
     */
    void purgeTilePathRam(const QString &path);
    /** Drop tile sessions on live + stashed items (before session-wide invalidateAll). */
    void dropAllTileLodSessions();
    void tickSlideshowPhaseMotionClocks();
    void tickSlideshowDwellMotionClock();
    void tickSlideshowMotion();
    /** Static centre-crop cover of @p image to the current viewport size. */
    QPixmap renderCoverPixmap(const QImage &image) const;
    /**
     * Ken Burns / pan-scan sample of @p image at motion progress @p motionT in
     * [0, 1]. Used for dwell blit and dual-image crossfade frames.
     */
    QPixmap renderMotionCoverPixmap(const QImage &image, qreal motionT,
                                    uint pathHash) const;
    /** Software snapshot of the current slide (no QOpenGLWidget::grab). */
    QPixmap captureSlideshowFrame() const;
    /** Draw Ken Burns / pan-scan using a pre-scaled atlas (cheap per-frame blit). */
    /**
     * Draw Ken Burns / pan-scan. Camera geometry uses the path's *logical*
     * image size (m_sizeBook / thumtoo cache), never the raster's pixel
     * dimensions — soft placeholders are sampling only.
     */
    QSize resolveMotionLogicalSize(const QString &path) const;
    QRectF computeMotionCoverDestRect(qreal iw, qreal ih, int vw, int vh,
                                      qreal motionT, QPointF biasA, QPointF biasB,
                                      const QString &path) const;
    tilelod::TileLodController *slideshowTilesForPath(const QString &path) const;
    bool paintSlideshowTiles(QPainter *painter, const QString &path,
                             const QRectF &dest, const QImage &underlay) const;
    void paintMotionCover(QPainter *painter, const QImage &image, qreal motionT,
                          QPointF biasA, QPointF biasB,
                          const QString &path = QString()) const;
    /**
     * Draw cover-scaled blurred @a image into @a viewportRect (ZoomBlur fill).
     * @a stableKey must identify the slide for the current viewport size
     * (path hash + dimensions). Do not use QImage::constBits() — buffers are
     * often reallocated while pixels stay the same, which forced a full CPU
     * blur every paint and spiked transitions.
     */
    bool zoomBlurKeyCached(qint64 key) const;
    bool zoomBlurKeyInFlight(qint64 key) const;
    int claimZoomBlurFlightSlot(qint64 key) const;
    void installZoomBlurResult(const QImage &blurred, qint64 key, quint64 gen);
    void scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const;
    void clearSlideshowZoomBlurSlots();
    void invalidateZoomBlurQueue() const;
    void paintZoomBlurUnderlay(QPainter *painter, const QImage &image,
                               const QRect &viewportRect, qint64 stableKey) const;

    /**
     * Build/refresh motion atlas: sized from viewport × motion headroom
     * (resolution-invariant), not from source pixel dimensions.
     */
    bool phaseBufferWantsSample(const QString &path, int sampleEdge) const;
    bool snapshotSlideshowContentAppearance(const QString &path,
                                            WorkspaceItemState *out) const;
    void scheduleSlideshowPhaseBufferUpgrade(const QString &path, const QImage &image);
    void finishSlideshowPhaseBufferUpgrade(const QString &path, const QImage &oriented,
                                           quint64 generation);
    // SlideshowAtlasKind: slideshowtypes.h
    void requestDwellAtlasRebuild();
    void requestToPhaseAtlasRebuild();
    void requestSlideshowAtlas(SlideshowAtlasKind kind);
    void finishSlideshowAtlas(SlideshowAtlasKind kind, quint64 generation,
                              const QImage &scaled, qreal atlasScale,
                              int atlasVw, int atlasVh);
    void finishDwellAtlasRebuild(quint64 generation, const QImage &scaled,
                                qreal atlasScale, int atlasVw, int atlasVh);
    /** Viewport × motion headroom → atlas budget (see SlideshowAtlasPolicy). */
    DwellAtlasParams dwellAtlasParams() const;
    void invalidateDwellAtlasRebuilds();
    void ensureMotionAtlas(const QImage &image, QPixmap *atlas, qreal *atlasScale,
                           int *atlasVw, int *atlasVh) const;
    /** Store sample in ImageCache (upward-only long edge). */
    void putSlideshowRaster(const QString &path, const QImage &image);
    /**
     * PreferCache / ladderReady: ImageCache put + upgrade phase buffers when
     * the path is the current from/to pair (logical-size camera).
     */
    void onSlideshowRasterReady(const QString &path, const QImage &image);
    /**
     * Top-left chip while slideshow is warming target-edge rasters (queue or
     * current phase still soft). Empty when idle / not in slideshow.
     */
    QString slideshowPrefetchHudLine() const;
    /** Dedicated HUD line: Loading · N active · cache vs file/archive. */
    QString loadingStatusHudLine() const;
    /** Best unoriented host sample for path (ImageCache), or null. */
    QImage slideshowRaster(const QString &path) const;
    void setSlideshowUnderlayVisible(bool visible);
    void hideSlideshowUnderlay();
    /** Fit / Fill / 1:1 framing for a slideshow slide (motion off). */
    void applySlideshowZoomFraming(ImageItem *item);
    /** Controller host: session path order used for Gallery packing. */
    const QStringList &pathOrder() const { return m_pathOrderBook.pathList(); }
    /** How many times @a path appears in session path order (duplicate tiles). */
    int pathOrderOccurrences(const QString &path) const;
    const QVector<SessionImageId> &sessionIdOrder() const { return m_pathOrderBook.idList(); }
    void clearPathOrder() { m_pathOrderBook.clear(); }
    void setPathOrder(const QStringList &paths)
    {
        m_pathOrderBook.setOrder(paths, QVector<SessionImageId>());
    }
    void setPathOrder(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        m_pathOrderBook.setOrder(paths, ids);
    }
    /** Controller host: disable Image-mode fit/fill when restoring free-form. */
    void clearFitFillModes();
    /** Re-apply scrollbar policies so AsNeeded ranges update after fit/zoom. */
    void refreshScrollBarGeometry();
    /** Controller host: Image/Gallery soft reset to fit, not fill. */
    void enableFitMode();
    /** Controller host: session appearance store (id-keyed). */
    SessionAppearanceStore &appearance() { return m_appearance; }
    const SessionAppearanceStore &appearance() const { return m_appearance; }
    /** Controller host: path-keyed placement / unbound appearance cache. */
    void setItemStateForPath(const QString &path, const WorkspaceItemState &state)
    {
        m_itemStateBook.set(path, state);
    }

    const WorkspaceItemState *itemStateForPath(const QString &path) const
    {
        return m_itemStateBook.get(path);
    }
    bool hasPendingWorkspacePaths() const { return m_loadGate.hasPendingWorkspacePaths(); }
    void clearPendingWorkspacePaths() { m_loadGate.clearPendingWorkspacePaths(); }
    void addPendingWorkspacePath(const QString &path) { m_loadGate.addPendingWorkspacePath(path); }
    void takePendingWorkspacePath(const QString &path);
    void setPendingRestoreStates(const QList<WorkspaceItemState> &states)
    {
        m_loadGate.setPendingRestoreStates(states);
    }
    /** Claim one pending restore snapshot for @a path (FIFO; duplicates OK). */
    bool takePendingRestoreState(const QString &path, WorkspaceItemState *out);
    /**
     * LoadReplace: Image-mode navigation install, or empty multi-item seed.
     */
    void completeLoadReplace(const QString &path, const QImage &image, quint64 generation);
    /** LoadRestore: create tile from decode + claimed restore snapshot. */
    void completeLoadRestore(const QString &path, const QImage &image);
    /**
     * LoadAdd: fill/create tiles for @a path after a successful or failed decode.
     * Honours load generation and pending-workspace cancellation.
     */
    void completeLoadAdd(const QString &path, const QImage &image, quint64 generation);

    // --- completeLoadAdd phases (private; keep the driver thin) ---
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

    // --- completeLoadReplace phases ---
    void installImageModeReplaceItem(const QString &path, const QImage &image);
    void seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image);

    // --- Controller host operations (mode controllers; prefer these over friend) ---
    /** Apply interactive/gallery/static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item);
    /** Gallery stash restore: bake session crop/orient onto the tile if needed. */
    void rematerializeGalleryItemFromStore(ImageItem *item);
    /**
     * Controller host: budgeted tile pump/issue for viewport items.
     * Gallery restore uses this so Image-mode path RAM paints without waiting
     * for the decode-window timer.
     */
    void tickPrimaryTileLod(int budget = 8);
    /** @deprecated Path is not identity; prefer findItemBySessionId. */
    ImageItem *findItemByPath(const QString &path) const;
    /**
     * Resolve a path to a live item without always taking the first match.
     * Prefer a selected item with that path, else the sole live match.
     * Returns nullptr when ambiguous (multiple unselected matches) or none.
     */
    ImageItem *findPreferredItemForPath(const QString &path) const;
    ImageItem *targetItem() const;
    void destroyCanvasItem(ImageItem *item);
    void updateWorkspaceSceneRect();
    /** Queue LoadRestore for workspace rebuild from durable snapshot. */
    void scheduleRestoreLoad(const QString &path);
    void snapshotWorkspace();
    void discardStashedWorkspace();
    /** Clear durable Workspace arrangement (session open ≠ project). */
    void clearDurableWorkspaceSnapshot();
    void snapshotFreeFormStates();

    /** Destroy live canvas items only; keep Workspace/Gallery stashes. */
    void clearLiveCanvas();

    /** Workspace: show a paper-sized frame (scene units) for print layout. */
    void setPageGuideVisible(bool on);
    bool pageGuideVisible() const { return m_pageGuide.isVisible(); }
    /** Update guide size from a printer page layout (millimetres → scene px). */
    void setPageGuideFromPrinter(const QPrinter &printer);
    /** Size page guide to live content bounds (+ margin); shows the guide. */
    void fitPageGuideToContent(qreal marginPx = 16.0);
    /** Scene rectangle of the page guide (printer: centred; fit-content: content AABB). */
    QRectF pageGuideSceneRect() const;
    bool pageGuideSelected() const { return m_pageGuide.isSelected(); }
    void setPageGuideSelected(bool on);
    /** Scene pixels per millimetre for the page guide (layout scale). */
    static qreal pageGuidePxPerMm();
    /** Paint current mode content into @p pageRect on @p painter (print/preview). */
    void renderForPrint(QPainter *painter, const QRectF &pageRect) const;
    /** Scene bounds of live image tiles (no chrome), with a small margin. */
    QRectF contentExportBounds() const;
    /** Rasterize @p sourceSceneRect into an image of @p pixelSize. */
    QImage renderExportImage(const QSize &pixelSize, const QRectF &sourceSceneRect,
                             bool transparentBackground = true) const;
    /** Drop cached Gallery tiles (e.g. after loading a new session). */
    void discardStashedGallery();

    void setViewMode(ViewMode mode);

    /**
     * Gallery or Workspace → Image (Phase 3).
     * Gallery: snapshot viewport then stash tiles. Workspace: durable snapshot
     * + stash (inside setViewMode). Caller loads the focused path afterward.
     */
    void leaveForImageMode();
    /** @deprecated Prefer leaveForImageMode(); identical behaviour. */
    void leaveGalleryForImage() { leaveForImageMode(); }

    /**
     * Image → Gallery transition (Phase 3).
     * Arms viewport restore from the leave snapshot, enters Gallery with
     * @p layout, and applies the pending centre/scroll when possible.
     * Caller may still populate session paths (populateGalleryCanvas).
     */
    void returnToGalleryFromImage(LayoutMode layout, const QString &focusPath = QString());

    /**
     * Image → Workspace transition (Phase 3).
     * Restores stashed free-form tiles (or durable snapshot) via setViewMode.
     */
    void returnToWorkspaceFromImage();
    /** Reset view/scene so Image mode is not affected by prior canvas state. */
    void prepareImageModeCanvas();
    void prepareGalleryCanvas();
    ViewMode viewMode() const { return m_viewMode; }
    bool isImageMode() const { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const { return m_viewMode == ViewMode::Workspace; }
    bool isMultiItemMode() const { return m_viewMode != ViewMode::Image; }

    /** @deprecated Alias for isWorkspaceMode(). */
    bool workspaceMode() const { return isWorkspaceMode(); }


    /**
     * Enable left/right edge navigation affordances in Image mode.
     * Typically true when the session has more than one image.
     */
    void setImageModeNavigationEnabled(bool on);
    /** When true, Image mode top edge offers return-to-gallery. */
    void setGalleryReturnAvailable(bool on);
    bool galleryReturnAvailable() const { return m_sessionNav.isGalleryReturnAvailable(); }
    bool imageModeNavigationEnabled() const { return m_sessionNav.isImageModeNavEnabled(); }

    /**
     * Show exactly the given paths on the workspace. Images already present
     * keep their live transform; newly added ones restore saved state or get
     * a default placement. Images no longer listed are removed from the
     * scene after their state is saved.
     */
    void setWorkspacePaths(const QStringList &paths);
    /**
     * Same as path-only overload, but binds each path to the parallel
     * @p sessionIds entry (stable SessionImageId). Gallery/Workspace loads
     * then apply per-id crop/flip/rotate from m_appearance.
     */
    QList<ImageItem *> collectDoomedWorkspaceItems(const QStringList &paths,
                                                   const QVector<SessionImageId> &sessionIds) const;
    void destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed);
    void finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                 const QVector<SessionImageId> &sessionIds);
    /** Create Gallery placeholders for m_pathOrder using definitive sizes only. */
    void ensureGalleryPlaceholders();
    void setWorkspacePaths(const QStringList &paths,
                           const QVector<SessionImageId> &sessionIds);
    /** Reorder canvas items to match @p paths (session / sort order). */
    void reorderItemsByPaths(const QStringList &paths);

    /**
     * While true, Gallery ignores resize/debounce-driven applyLayout (used
     * during session delete so the pack and scroll stay put).
     */
    void setGalleryRelayoutSuppressed(bool on);
    bool galleryRelayoutSuppressed() const { return m_galleryRelayoutSuppress.active(); }

    /**
     * Reload from disk: Image mode — current session image only;
     * Gallery — re-decode all tiles (keeps positions unless @p relayout);
     * Workspace — re-decode on-canvas items in place.
     */
    void reloadFromDisk(bool relayoutGallery = true);
    /** When true, destroyCanvasItem does not clear the undo stack (session remove). */
    void setPreserveUndoOnDestroy(bool on) { m_preserveUndoOnDestroy = on; }
    /**
     * Gallery: scroll/HUD focus for session cursor without collapsing multi-select.
     * (focusSessionPath exclusive-selects — for keyboard nav.)
     */
    void revealGalleryPath(const QString &path);
    /** Remove one image from the workspace, remembering its transform. */
    void removeWorkspacePath(const QString &path);
    /** Remove the canvas item bound to this session slot (duplicate-safe). */
    void removeWorkspaceSessionIndex(int sessionIndex);
    bool hasWorkspaceSessionIndex(int sessionIndex) const;
    ImageItem *findItemBySessionIndex(int sessionIndex) const;
    ImageItem *findItemBySessionId(SessionImageId sessionId) const;
    /** True while LoadAdd still has an unbound PendingSessionBind for @p path. */
    bool hasPendingSessionBindForPath(const QString &path) const;
    void removeWorkspaceSessionId(SessionImageId sessionId);
    QList<ImageItem *> collectItemsForSessionId(SessionImageId sessionId) const;
    QStringList destroySessionIdItems(const QList<ImageItem *> &doomed);
    void prunePendingBindsAndSavedForSessionId(SessionImageId sessionId);
    void prunePathOrdersAfterSessionRemove(const QStringList &removedPaths);
    void restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                           const QPointF &keptCenter, int scrollH, int scrollV);
    /** Hide canvas tile(s) for @p sessionId without dropping session appearance. */
    void detachCanvasSessionId(SessionImageId sessionId);
    /** Assign sequential session indices to currently selected items starting at @p first. */
    void bindSelectedSessionIndices(int firstSessionIndex);
    /** Bind selected canvas items to stable session ids (same order). */
    void bindSelectedSessionIds(const QList<SessionImageId> &ids);
    /** How many canvas items currently show @p path. */
    int workspacePathOccurrenceCount(const QString &path) const;
    /** Remove the n-th canvas item with @p path (0-based, m_items order). */
    void removeWorkspacePathOccurrence(const QString &path, int occurrence);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    QUndoStack *undoStack() const { return m_undoStack; }

    void zoomIn();
    void zoomOut();
    void zoomReset();
    /** View scale ≈41% (four ×0.8 zoom-out steps) for Workspace overview. */
    void setWorkspaceDefaultViewScale();
    void zoomFit();
    /** Cover the viewport (may crop); uses KeepAspectRatioByExpanding. */
    void zoomFill();
    /**
     * Sticky framing (Image mode only): Fit / Fill / 1:1 stay active across
     * Image-mode navigation until toggled off or free zoom (+/−, wheel, region)
     * runs. Gallery and Workspace use one-shot zoom + ensureVisible; sticky is
     * released when leaving Image mode.
     */
    // StickyZoomKind: viewframing.h
    void setStickyZoomEnabled(bool on);
    void releaseStickyZoom();
    void captureStickyZoomFromCurrentFraming();
    bool stickyZoomEnabled() const { return m_framing.isStickyZoomEnabled(); }
    void setStickyZoomKind(StickyZoomKind kind);
    StickyZoomKind stickyZoomKind() const { return m_framing.currentStickyZoomKind(); }
    /** Image-mode framing after soft/full install (honours sticky zoom). */
    void applyImageModeFraming(ImageItem *item);
    /** Best-effort: remember viewport centre in image-normalized coords. */
    void captureStickyPanAnchor(ImageItem *item);
    void restoreStickyPanAnchor(ImageItem *item);
    /**
     * One-shot rubber-band zoom: next left-drag selects a region to zoom into.
     * Esc cancels. Bound to Z from the main window.
     */
    void armZoomRegion();
    void cancelZoomRegion();
    bool zoomRegionArmed() const { return m_zoomRegion.isArmed(); }
    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;
    void rotateLeft();
    void rotateRight();
    /**
     * Sole content ±90° path (Workspace chrome, toolbar, keyboard).
     * ContentXform bake + mode framing. Prefer this over bakeItemRotate90
     * from UI code so chrome and shortcuts cannot diverge.
     */
    void rotateContentByQuarterTurns(ImageItem *item, int quarterTurns);
    void flipHorizontal();
    void flipVertical();
    /** True when rotate/flip have at least one target (selection or sole image). */
    bool hasTransformTargets() const;
    /** True when crop is allowed: exactly one transform target (not multi-select). */
    bool hasSingleCropTarget() const;

    /**
     * Interactive crop (Image mode, or a single Workspace target).
     * Toggle on: draft rect = full content; dim outside; edge/corner handles.
     * Enter applies pixel crop; Esc / toggle off cancels.
     */
    void setCropMode(bool on);
    bool resolveApplyHostAndState(ImageItem *item, QImage *host, bool *hostFromCache,
                                  WorkspaceItemState *st, SessionImageId *sid);
    bool completeCropEnterUnderHold(ImageItem *item,
                                    const QPointF &workspaceAnchorScene);
    bool enterCropModeFromUi();
    /**
     * Best host raster for crop / content bake / Workspace restore.
     * Prefer ImageCache when it already covers native logical size (after
     * Image-mode / thumtoo full climb); otherwise ImageLoader::load and put
     * into ImageCache. Avoids redundant full decodes on the GUI when the
     * session already holds native pixels.
     */
    QImage fullRasterForEdit(const QString &path) const;
    bool isCropMode() const { return m_crop.active(); }
    void toggleCropMode();

    /**
     * Attention / focus-point mode (Image mode). Multi-point overlay with
     * standard selection: click select, Shift/Ctrl toggle, drag empty =
     * rubber-band, Ctrl+click = insert, Del = delete, undoable edits.
     * Primary (first) point drives slideshow Ken Burns. Auto-detects peaks
     * when entering if none stored.
     */
    void setAttentionMode(bool on);
    bool isAttentionMode() const { return m_attention.active(); }
    void toggleAttentionMode();
    /** Re-run saliency detect on the current image (replaces all points). */
    void detectAttentionPoint();
    /** Undo/redo: replace attention points on the current attention target. */
    void restoreAttentionPoints(const QVector<QPointF> &pts);
    /** Commit the draft crop rect to pixels and leave crop mode. */
    void applyCrop();
    /** Shrink draft to non-background content (margin trim). */
    void requestCropViewportUpdate();
    void applyAutoCrop();
    /** Discard the draft and leave crop mode. */
    void cancelCrop();
    /** Restore pixels + session crop metadata (used by crop undo/redo). */
    void storeAppearanceFromState(ImageItem *item, const WorkspaceItemState &state);
    void relayoutAfterAppearanceApply(ImageItem *item);
    void applyCropAppearancePixels(ImageItem *item, const QImage &src,
                                   const WorkspaceItemState &state);
    void clearIdentityContentAppearance(ImageItem *item,
                                        const WorkspaceItemState &state);
    void applyCropAppearance(ImageItem *item, const QImage &src,
                            const WorkspaceItemState &state);

    /** When true (default), left-drag pans in Image mode. */
    void setImageModeLeftDragPan(bool on);

    /** Debug: paint text/link region rects for page documents (Image mode). */
    void setShowTextRegions(bool on);
    bool showTextRegions() const { return m_textLayer.showsRegions(); }
    void refreshTextLayer();

    /**
     * Highlight regions whose text contains @p query (case-insensitive).
     * Empty query clears highlights. Loads the text layer if needed.
     * Returns number of matching regions on the current page.
     */
    int setTextSearchQuery(const QString &query);
    QString textSearchQuery() const { return m_textLayer.searchQueryRef(); }
    int textSearchMatchCount() const { return m_textLayer.matchCount(); }
    bool hasTextLayer() const;
    int textLayerRegionCount() const;
    /** Soft match for OCR noise (alnum-only + light edit distance). Default on. */
    void setTextSearchFuzzy(bool on);
    bool textSearchFuzzy() const { return m_textLayer.isSearchFuzzy(); }
    /** True if @p regionText matches @p query under the same rules as Find. */
    static bool textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy);

    /** Selected region indices from Shift+drag rubber-band (Image mode page docs). */
    int textSelectionCount() const { return m_textLayer.selectionCount(); }
    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;
    /** Copy selected text to the clipboard; returns false if nothing selected. */
    bool copySelectedText();
    void clearTextSelection();
    /** Non-empty while the pointer is over a link region. */
    QString linkHoverTip() const { return m_textLayer.linkHoverTipRef(); }

    bool imageModeLeftDragPan() const { return m_chrome.isImageModeLeftDragPan(); }

    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_canvasBg.primaryColor(); }
    /**
     * Effective solid pad colour for slideshow letterbox (Solid mode colour,
     * else Preferences background). Used when ZoomBlur cannot run.
     */
    QColor slideshowPadColor() const;
    void setBackgroundColorAlt(const QColor &color);
    QColor backgroundColorAlt() const { return m_canvasBg.altColor(); }
    void setBackgroundPattern(BackgroundPattern pattern);
    BackgroundPattern backgroundPattern() const { return m_canvasBg.currentPattern(); }
    /** When true, checkerboard is used only in Workspace; other modes stay solid. */
    void setCheckerboardWorkspaceOnly(bool on);
    bool checkerboardWorkspaceOnly() const { return m_canvasBg.isCheckerWorkspaceOnly(); }

    /**
     * Per-Workspace background override (project state). AppDefault uses the
     * preference colours/pattern (technical default) instead of a custom look.
     */
    void setWorkspaceBackground(const WorkspaceBackground &bg);
    WorkspaceBackground workspaceBackground() const { return m_canvasBg.workspaceRef(); }
    void clearWorkspaceBackground(); /**< AppDefault */
    /**
     * Temporary view of the Preferences / technical background without changing
     * the project WorkspaceBackground override. Used by the Background Default
     * toolbar toggle.
     */
    void setWorkspaceBackgroundShowDefault(bool on);
    bool workspaceBackgroundShowDefault() const { return m_canvasBg.isWorkspaceShowDefault(); }

    /**
     * Session position for status line and HUD (index/total, 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     * @p pulseIdentity briefly shows filename/badge without a pinned HUD (H);
     * use false for silent updates (e.g. slideshow auto-advance).
     */
    void setSessionPosition(int index, int total, bool pulseIdentity = true);
    void setCurrentSessionId(SessionImageId id);
    SessionImageId currentSessionId() const { return m_sessionId.currentIdValue(); }
    /** Select canvas item for @p path; ensure visible in Gallery. */
    void focusSessionPath(const QString &path);
    int sessionIndex() const { return m_sessionId.currentIndex(); }
    int sessionTotal() const { return m_sessionId.currentTotal(); }

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    bool hudVisible() const { return m_hudPrefs.isVisible(); }
    /** Corner marks for crop / orient / grade (default on). */
    void setContentEditMarksVisible(bool on);
    /** Seed orient/flip/grade from path XDG for each session id (open/restart). */
    void seedSessionAppearancesFromPaths(const QStringList &paths,
                                         const QVector<SessionImageId> &ids);
    bool contentEditMarksVisible() const;
    void setHudFontPointSize(int pt);
    int hudFontPointSize() const { return m_hudPrefs.fontPointSizeValue(); }
    void setHudTextColor(const QColor &color);
    QColor hudTextColor() const { return m_hudPrefs.textColorRef(); }
    void setHudPanelColor(const QColor &color);
    QColor hudPanelColor() const { return m_hudPrefs.panelColorRef(); }

    /**
     * Brief top-left HUD action (slideshow, fit mode, …).
     * Visible for ~1.8s; never used for Next/Prev (session badge covers that).
     */
    void flashHud(const QString &action, const QString &detail = QString());

    /**
     * Slideshow dwell progress for the pinned HUD: a 1px line at the bottom of
     * the viewport. Call with active=true and the current interval when a slide
     * starts (or interval changes while running); active=false when the
     * slideshow stops. The line is drawn only while the full HUD is pinned.
     */
    void setSlideshowProgress(bool active, int intervalMs = 0);
    /**
     * Overall slideshow timeline for the extended (pinned) HUD — video-player
     * style elapsed / total and remaining. Pass totalMs<=0 to clear.
     * Drawn only while the full HUD is pinned and a slideshow is active.
     */
    void setSlideshowTimeline(qint64 elapsedMs, qint64 totalMs);
    /** Per-cycle phase in [0,1] from host unitless clock. */
    void setSlideshowCycleProgress(qreal phase01);

    void setSlideshowTransition(SlideshowTransition kind);
    SlideshowTransition slideshowTransition() const { return m_ssSettings.currentTransition(); }
    void setSlideshowTransitionDurationMs(int ms);
    int slideshowTransitionDurationMs() const { return m_ssSettings.transitionDuration(); }
    /** Clear residual transition overlay state (safe during pure-phase show). */
    void cancelSlideshowTransition();

    void setSlideshowMotion(SlideshowMotion mode);
    SlideshowMotion slideshowMotion() const { return m_ssSettings.currentMotion(); }
    void setPanZoomFactor(qreal factor);
    qreal panZoomFactor() const { return m_ssSettings.currentPanZoomFactor(); }

    void setSlideshowZoom(SlideshowZoom mode);
    SlideshowZoom slideshowZoom() const { return m_ssSettings.currentZoom(); }

    void setSlideshowLetterboxFill(SlideshowLetterboxFill mode);
    SlideshowLetterboxFill slideshowLetterboxFill() const { return m_ssSettings.currentLetterboxFill(); }
    /** Pad colour when letterbox fill is Solid (also fallback for ZoomBlur miss). */
    void setSlideshowPadColor(const QColor &color);
    void cancelSlideshowMotion();
    /**
     * Freeze or continue Ken Burns without tearing down the dwell camera.
     * Used for slideshow pause/resume (Space), not full stop.
     */
    void setSlideshowMotionPaused(bool paused);
    /**
     * Persistent top-left "Paused" cue while a slideshow session is paused.
     * Independent of flashHud (which times out after ~1s). Cleared on resume/stop.
     */
    void setSlideshowPausedHud(bool on);
    bool slideshowPausedHud() const { return m_ssHud.isPausedHud(); }
    /** Freeze/resume dwell progress elapsed without resetting the timeline. */
    void setSlideshowProgressPaused(bool paused);
    /** Image-mode fit after leaving slideshow (Fit to window). */
    void restoreImageFramingAfterSlideshow();
    /** Start dwell image-blit Ken Burns if enabled and slideshow is active. */
    void maybeStartSlideshowMotion();
    /**
     * Re-frame the current Image-mode item for an active slideshow:
     * motion on → restart Ken Burns blit from slideshow zoom base; motion off →
     * Fit / Fill / 1:1 framing only. No-op when slideshow progress is inactive.
     */
    void reapplySlideshowFraming();
    /**
     * Dwell + transition budget for one path's Ken Burns clock (min 250ms).
     * Used by phase arming and tickSlideshowMotion.
     */
    int slideshowPathDurationMs() const;
    /** Pure-clock drive: fadeT<0 dwell on fromPath; else crossfade from→to at fadeT in [0,1]. */
    bool applySlideshowFadeProgressOnly(qreal fadeT);
    void updateSlideshowPhaseMotionProgress(int pathMs);
    void setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT);
    void warmZoomBlurForCurrentPhase();
    /** Drop ZoomBlur slots whose key is neither from nor to path. */
    void pruneZoomBlurOutsidePhasePair(const QString &fromPath, const QString &toPath);
    /** Prefetch ZoomBlur underlay for a phase path if viewport is valid. */
    void schedulePhaseZoomBlur(const QString &path, const QImage &image);
    /**
     * Compute Ken Burns biases for @a path without clobbering the live
     * m_motionBias* pair used by the from-slot.
     */
    void captureMotionBiasesForPath(const QString &path, const QImage &image,
                                    QPointF *outA, QPointF *outB);
    /** Ensure 16ms PreciseTimer connected to tickSlideshowMotion. */
    void ensureSlideshowMotionTimer();
    /** Arm from-slot on path change (promote B or start); @a pathMs for motion. */
    bool shouldPromoteSlideshowToAsFrom(const QString &fromPath) const;
    void promoteSlideshowFromToPhase(const QString &fromPath);
    void startSlideshowFromPhase(const QString &fromPath);
    void prepareSlideshowFromDwell(const QString &fromPath);
    void armSlideshowMotionClock(int pathMs);
    void armSlideshowFromPhase(const QString &fromPath, int pathMs);
    /** Arm or clear to-slot on path change. */
    void armSlideshowToPhase(const QString &toPath);
    QImage slideshowSampleUnoriented(const QString &path) const;
    QImage slideshowPixelsForPath(const QString &path);
    QImage slideshowFullIfReady(const QString &path) const;
    QImage slideshowSoftPlaceholder(const QString &path);
    /** SessionImageId for a session path (path order), or invalid. */
    SessionImageId sessionIdForPath(const QString &path) const;
    /**
     * Apply path-keyed content appearance (flip / quarter-turns / grade) to
     * unbaked disk pixels for slideshow paint. Never use m_sessionId.currentIdValue() —
     * that is the *dwell* image during a live transition to another path.
     */
    QImage orientSlideshowImage(const QImage &raw, const QString &path) const;
    /**
     * Drop any held live-transition overlay once the next slide is fitted.
     * Called from the LoadReplace path so the incoming frame is not cleared
     * before the new item is on screen (avoids a one-frame flash of the old image).
     */

    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    void raiseSelected();
    void lowerSelected();
    /** Raise/lower by scene overlap (not abstract z step). */
    void raiseItem(ImageItem *item);
    void lowerItem(ImageItem *item);
    void opacityUp();
    void opacityDown();
    void opacityReset();
    void resetItemScale();
    void resetItemRotation();
    void resetItemShear();
    /** Workspace: clone selection (same path, independent transforms). */
    void duplicateSelected();

    /**
     * Workspace clipboard: capture selected tiles (path + content + pose).
     * Empty when not Workspace or nothing selected.
     */
    QList<WorkspaceItemState> captureSelectedWorkspaceClipboard() const;
    /** Workspace: remove selected tiles from the canvas only (session kept). */
    void removeSelectedCanvasItems();
    /**
     * Place clipboard tiles. @p items already include paste pose offset;
     * @p newIds / sessionIndices are parallel. Caller must setSessionAppearance
     * before this so LoadAdd applies content + pose from the store.
     */
    void placeWorkspaceClipboardItems(const QList<WorkspaceItemState> &items,
                                      const QVector<SessionImageId> &newIds,
                                      const QList<int> &sessionIndices);

    /** Remove canvas tiles for @p ids (pose remembered). Session rows stay. */
    void removeCanvasSessionIds(const QList<SessionImageId> &ids);
    /** Place tiles for existing session ids using appearance store + path. */
    void placeSessionIdsOnCanvas(const QList<SessionImageId> &ids,
                                 const QStringList &paths,
                                 const QList<int> &sessionIndices);


    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layout.currentMode(); }
    GalleryLayout::Mode galleryLayoutModeFromViewMode() const;
    void applyLayout(GalleryPackReason reason = GalleryPackReason::ExplicitLayout);
    /**
     * Coalesce rapid Gallery packs (e.g. sizeReady storm when opening a large
     * archive). Runs applyLayout after a short idle; Enter/Explicit callers
     * should still use applyLayout directly.
     */
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);
    /**
     * Workspace only: pack @p items (or current selection) with a packaged
     * layout without leaving FreeForm / Workspace mode. Returns false if
     * there is nothing to arrange.
     */
    bool layoutWorkspaceItems(const GalleryLayout::Params &params,
                              const QList<ImageItem *> &items = {});
    void applyPendingGalleryRestore();
    /** Gallery mode with a packaged layout. */
    bool isGalleryLayout() const { return isGalleryMode(); }

    /** Enter Gallery mode and apply the given packaged layout (not FreeForm). */
    void enterGallery(LayoutMode packagedLayout);

    /** Remember gallery scroll position (call before leaving Gallery for Image). */
    void snapshotGalleryViewport();
    /**
     * After gallery items are laid out, select @p focusPath (if present) and
     * restore the last snapshot scroll, then ensure the focused item is visible.
     */
    void restoreGalleryViewport(const QString &focusPath = QString());
    /**
     * Re-apply the last Gallery viewport snapshot (centre + scroll) without
     * packing or ensureVisible. Used after session delete / splitter churn.
     */
    void reassertGalleryViewport();

    /** Number of columns for LayoutMode::Masonry (images scale to fit column width). */
    void setMasonryColumns(int columns);
    int masonryColumns() const { return m_layout.masonryColumnsValue(); }
    /** Grid / GridCrop columns; 0 = automatic. */
    void setGridColumns(int columns);
    int gridColumns() const { return m_layout.gridColumnsValue(); }

    /** Number of rows for LayoutMode::MasonryRows (images scale to fit row height). */
    void setMasonryRows(int rows);
    int masonryRows() const { return m_layout.masonryRowsValue(); }

    WorkspaceItemState captureState(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);
    /** Persist session state and refresh filmstrip (chrome / toolbar edits). */
    void commitItemSessionEdit(ImageItem *item);
    void persistSessionAppearanceSlot(ImageItem *item);
    void syncSessionEditPeers(ImageItem *item);
    void updateWorkspaceSavedAppearance(ImageItem *item);
    /**
     * After content appearance changed for @p item: filmstrip id override,
     * Gallery pack, Workspace sceneRect, or Image mode sceneRect.
     * Called from commitItemSessionEdit — keep mode widgets in sync.
     */
    void propagateSessionAppearanceToViews(ImageItem *item);
    QImage sessionAppearanceImage(const ImageItem *item) const;
    /**
     * Bake stored content flips/rotates (and grade) onto a decode of @p path
     * for session @p sid. Used for soft placeholders and slideshow cache blits.
     * Crop is skipped without full-source geometry (flip/rotate only).
     */
    QImage imageWithSessionAppearance(const QImage &src, SessionImageId sid,
                                      const QString &path = QString()) const;
    /** Copy of stored appearance for @p id (empty/default if none). */
    WorkspaceItemState sessionAppearanceValue(SessionImageId id) const;
    bool hasSessionAppearance(SessionImageId id) const;
    /** Restore appearance after session undo (store only; no canvas mutate). */
    void setSessionAppearance(SessionImageId id, const WorkspaceItemState &state);
    void copySessionAppearance(SessionImageId fromId, SessionImageId toId);
    void setTargetColorAdjustments(const ColorAdjustments &adj);
    ColorAdjustments targetColorAdjustments() const;
    /** Interactive grade: clamped host bake (no SQLite / filmstrip). */
    void applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want);
    /** After slider idle: durable save, filmstrip, full rematerialize if needed. */
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);
    void flushColorAdjustCommit();
    /** Bake ±90° content into pixels and session state (not placement). */
    WorkspaceItemState captureContentBakeBeforeState(ImageItem *item) const;
    SessionImageId resolveContentEditSessionId(ImageItem *item) const;
    WorkspaceItemState appearanceCropMapForEdit(ImageItem *item,
                                                const WorkspaceItemState &fallback,
                                                SessionImageId sid) const;
    void persistDurableContentAppearance(ImageItem *item, const WorkspaceItemState &s,
                                         const char *debugTag);
    void bakeItemRotate90(ImageItem *item, int quarterTurns);
    /** Bake flip into pixels and session state. */
    void bakeItemFlip(ImageItem *item, bool horizontal, bool vertical);
    /** Push geometry-only undo (pos/scale/rotation/opacity/z/item flips). */
    void pushItemGeometryCommand(const QString &text, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after);
    /** Push content bake undo (pixels + session appearance). */
    void pushItemContentCommand(const QString &text, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &before,
                                const WorkspaceItemState &after);

    /**
     * Single gate for attaching *raw* decode pixels to a session image.
     *
     * Sole host→display install gate (CONTENT_PIPELINE install invariant).
     * @p pixels are unoriented host. MaterializeDisplay(host, store want) then
     * attachDisplaySample. Multi-MP want → SoftPreview stand-in + async full.
     * SoftPreview includes scaled crop. Layout via layoutSize(native, want).
     * Already-baked pixels (peer copy, undo after-image, duplicate) must use
     * attachDisplaySample only — never this function.
     */
    void installDisplayPixels(ImageItem *item, const QImage &pixels,
                              SessionAppearance::PixelKind kind,
                              SessionImageId sid);
    /**
     * installDisplayPixels + preserveImageViewOnLogicalSizeChange.
     * Image-mode soft→full / soft→soft in-place upgrades (no zoom jump).
     */
    void installDisplayPreservingView(ImageItem *item, const QImage &pixels,
                                      SessionAppearance::PixelKind kind,
                                      SessionImageId sid);

    /**
     * Rematerialize display from ImageCache / item host for @p want.
     * Soft stand-in + async full when multi-MP. Public for WorkspaceController
     * leave→enter restore when fullRasterForEdit misses.
     */
    void rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want);

    /**
     * True when the primary transform target has non-identity content
     * appearance (session store and/or durable XDG state for its path).
     */
    bool targetHasContentAppearance() const;

    /**
     * Clear content appearance (flip / quarter-turns / crop) for transform
     * targets: durable state, session store, and reload full on-disk pixels.
     * Does not touch Workspace placement. Returns number of items reset.
     */
    int resetContentAppearanceForTargets();

    QString statusText() const;
    void appendThumtooDebugStatus(QString *text, ImageItem *item) const;
    QString statusTextEmpty() const;
    QString statusTextMultiItem(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;
    QString imageModeClimbActivityLabel(const ImageItem *item) const;
    QString statusTextImageMode(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;
    /** User-facing quality of pixels currently shown for @p item. */
    QString pixelQualityLabel(const ImageItem *item) const;
    /** Session badge for the top-right HUD, e.g. "[3/12]", or empty. */
    QString sessionBadgeText() const;
    /** Path of the last failed Image-mode decode (empty if none). */
    QString lastLoadError() const { return m_sessionId.lastLoadErrorRef(); }
    /** Basename of the current/target image for the bottom HUD. */
    QString hudFileName() const;
    ImageMouseInfo mouseInfo() const { return m_chrome.currentMouseInfo(); }
    QString currentPath() const;
    QSize imageSize() const;
    int itemCount() const;
    /** Live tiles, stashed tiles, or durable snapshot — Workspace is non-empty. */
    bool hasWorkspaceContent() const;
    QStringList itemPaths() const;
    /** Paths of selected canvas items (Gallery/Workspace). Image mode: current path. */
    QStringList selectedPaths() const;
    /** Select every live canvas tile (Gallery / Workspace). No-op in Image mode. */
    void selectAllCanvasItems();
    /** Clear canvas selection. */
    void clearCanvasSelection();
    /** In-flight LoadAdd / LoadRestore / viewport-window decodes. */
    int pendingDecodeCount() const;
    /** No-op: Gallery does not use setInterest (tiles + LQIP only). */
    void publishGalleryInterest(const QStringList &interestNear,
                                const QStringList &interestRest);
    void scheduleIdleGalleryDecodes(const QStringList &rest);
    void updateGalleryDecodeWindow();
    /**
     * Decode-window pass 1: attach ImageCache soft onto blank tiles (budgeted).
     * @return number of installs; @p morePending if the budget was exhausted.
     */
    int galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending);
    /** Coalesce decode-window rescans (setInterest + schedule) off the hot path. */
    void scheduleGalleryDecodeWindowRefresh(int delayMs = 48);
    /** Coalesce statusChanged during soft climb (MainWindow is not free). */
    void scheduleGalleryStatusRefresh(int delayMs = 100);

signals:
    void stickyZoomChanged();
    void statusChanged();
    /** Packaged Gallery: all session size probes settled (or timed out). */
    void gallerySizeResolveFinished();
    void mouseInfoChanged(const ImageMouseInfo &info);
    void toolChanged(ImageView::Tool tool);
    /** Emitted when items are removed from the workspace (e.g. Delete key). */
    void workspacePathsChanged();
    /** Workspace/Gallery canvas selection changed (session-index aware). */
    void canvasSelectionChanged();
    /** Image mode: user activated previous / next via edge click. */
    void navigatePreviousRequested();
    /** Slideshow: left-click centre (not edge zones) toggles pause. */
    void slideshowTogglePauseRequested();
    /** Seek fraction [0,1] of the full session timeline (mpv-style bar). */
    void slideshowSeekRequested(qreal fraction);
    void navigateNextRequested();
    /** Internal page (1-based) and/or external URI from a link region click. */
    void linkActivated(int page_1based, const QString &uri);
    /** Image mode: user activated top-edge return (Gallery or Workspace). */
    void galleryReturnRequested();
    /** Image mode: double-click requests fullscreen toggle. */
    void fullscreenToggleRequested();
    /** Black exit veil finished — host should advance the slideshow. */
    /** Host may restart the advance timer (snapshot end / fade-black complete). */
    void slideshowDwellResumeRequested();
    /** Crop mode toggled on/off (toolbar checkable state). */
    void cropModeChanged(bool active);
    void attentionModeChanged(bool active);
    /** Session crop committed; @p image is the new displayed pixels for @p path. */
    void sessionCropApplied(const QString &path, const QImage &image);
    void sessionCropApplied(SessionImageId sessionId, const QString &path, const QImage &image,
                           bool hasCrop);
    /** Flip / rotate / crop appearance for filmstrip (may include baked transforms). */
    void sessionAppearanceChanged(const QString &path, const QImage &image);
    void sessionAppearanceChanged(SessionImageId sessionId, const QString &path, const QImage &image);
    /**
     * Gallery layout: user clicked an item to open it in Image mode.
     * Path is the image file path.
     */
    void galleryItemOpenRequested(const QString &path);
    /**
     * Open the session slot @p sessionIndex in Image mode (duplicate-safe).
     * Prefer this over path-only open when the canvas item is session-bound.
     */
    void sessionSlotOpenRequested(int sessionIndex);
    /** Open session image by stable id (preferred over index). */
    void sessionImageOpenRequested(SessionImageId sessionId);
    /** Gallery focus moved (path fallback when tile is unbound). */
    void galleryItemFocused(const QString &path);
    /** Gallery focus by stable session id (preferred when tile is bound). */
    void sessionImageFocused(SessionImageId sessionId);
    /** Gallery: remove selected session images by id (duplicate-safe). */
    void sessionRemoveIdsRequested(const QVector<SessionImageId> &ids);
    /** Gallery: path-only remove for unbound tiles. */
    void sessionRemovePathsRequested(const QStringList &paths);
    /** File URLs dropped onto the view (same semantics as MainWindow). */
    void filesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                      const QPointF &scenePos, bool hasScenePos,
                      const QList<qint64> &sessionIds = {},
                      const QStringList &internalPaths = {});

public slots:
    /** Deliver a finished background decode (generation must still match). */
    void onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                       int role);
    /** Fast downscaled stand-in before the full decode arrives. */
    void onImagePreviewLoaded(const QString &path, const QImage &image, quint64 generation,
                              int role);
    /**
     * thumtoo PreferCache / soft / overview delivery (GUI thread after queue).
     * Seeds ImageCache; upgrades Image mode, slideshow, and Gallery soft state.
     */
    void onLadderReady(const QString &path, int maxEdge, const QImage &image);
    /** True while @p gen is still the active LoadReplace generation (pool jobs). */
    bool matchesLoadGeneration(quint64 gen) const
    {
        return m_loadGate.accepts(gen);
    }

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void paintViewportOverlays(QPainter &painter);
    void paintTextRubberBandOverlay(QPainter &painter);
    void paintWorkspaceViewportChrome(QPainter &painter);
    /** Gallery multi-select frames in scene space (not inside item paint cache). */
    void paintGallerySelectionFrames(QPainter *painter, const QRectF &exposed) const;
    void paintSlideshowLetterboxComposite(QPainter &painter);
    void paintEmptySessionInvite(QPainter &painter);
    void paintHudPanels(QPainter &painter);
    void paintSlideshowSeekbar(QPainter &painter);
    void recomputeTextSearchMatches();
    void finishTextRubberBand();
    [[nodiscard]] bool pageYUpForTextLayer() const;
    /**
     * Map a page text/link region into the primary item's current image-pixel
     * space (after session content flip / quarter-turn bake). Empty if the
     * layer or item is unavailable.
     */
    [[nodiscard]] QRectF textRegionImageRect(const ThumtooCache::TextRegion &region) const;
    /** Map viewport rubber rect → image-pixel rect on primary item. */
    [[nodiscard]] QRectF textRubberBandImageRect() const;
    /** Hit-test link region under view pos; sets page/uri outs. */
    [[nodiscard]] bool hitTextLinkAt(const QPoint &viewPos, int *pageOut,
                                    QString *uriOut) const;

    void drawBackground(QPainter *painter, const QRectF &rect) override;
    /** Scene-space canvas background (AppDefault or Workspace override). */
    void paintCanvasBackground(QPainter *painter, const QRectF &rect, qreal viewScale);
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    // mousePressEvent phases (return true if the event was handled)
    bool tryMousePressSlideshowSeek(QMouseEvent *event);
    bool tryMousePressAttention(QMouseEvent *event);
    bool tryMousePressCrop(QMouseEvent *event);
    bool tryMousePressZoomRegion(QMouseEvent *event);
    bool tryMousePressWorkspaceChrome(QMouseEvent *event);
    bool tryMousePressImageLink(QMouseEvent *event);
    bool tryMousePressTextRubber(QMouseEvent *event);
    bool tryMousePressImageEdges(QMouseEvent *event);
    bool tryMousePressPan(QMouseEvent *event);
    bool tryMousePressWorkspaceRotate(QMouseEvent *event);
    bool tryMousePressGalleryRight(QMouseEvent *event);
    bool tryMousePressGalleryLeft(QMouseEvent *event);
    bool tryMousePressWorkspaceSelect(QMouseEvent *event);

    // mouseMoveEvent phases
    bool tryMouseMoveTextRubber(QMouseEvent *event);
    void updateMouseMoveLinkHover(QMouseEvent *event);
    bool tryMouseMoveAttention(QMouseEvent *event);
    bool tryMouseMoveCropDrag(QMouseEvent *event);
    bool tryMouseMovePan(QMouseEvent *event);
    bool tryMouseMoveCropHover(QMouseEvent *event);
    bool tryMouseMoveZoomRegion(QMouseEvent *event);
    bool tryMouseMovePageGuide(QMouseEvent *event);
    bool tryMouseMoveGroupAndHandleDrag(QMouseEvent *event);
    bool tryMouseMoveWorkspaceRotate(QMouseEvent *event);
    void updateMouseMoveSlideshowSeek(QMouseEvent *event);
    void updateMouseMoveWorkspaceChromeHover(QMouseEvent *event);

    // mouseReleaseEvent phases
    void restoreToolCursor();
    void pushItemTransformUndo(ImageItem *item, const WorkspaceItemState &before,
                               const WorkspaceItemState &after, const QString &text);
    bool tryMouseReleaseSlideshowSeek(QMouseEvent *event);
    bool tryMouseReleaseTextRubber(QMouseEvent *event);
    bool tryMouseReleaseAttention(QMouseEvent *event);
    bool tryMouseReleaseCrop(QMouseEvent *event);
    bool tryMouseReleaseZoomRegion(QMouseEvent *event);
    bool tryMouseReleasePageGuide(QMouseEvent *event);
    bool tryMouseReleaseGroupDrag(QMouseEvent *event);
    bool tryMouseReleaseHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseWorkspaceRotate(QMouseEvent *event);
    bool tryMouseReleasePan(QMouseEvent *event);
    bool tryMouseReleaseItemDrag(QMouseEvent *event);

    // keyPressEvent phases
    bool tryKeyPressAttention(QKeyEvent *event);
    bool tryKeyPressCrop(QKeyEvent *event);
    bool tryKeyPressZoomRegion(QKeyEvent *event);
    bool tryKeyPressSelectAll(QKeyEvent *event);
    bool tryKeyPressImageNavigate(QKeyEvent *event);
    ImageItem *selectedOrFirstGalleryItem() const;
    void emitGalleryItemFocus(ImageItem *item);
    bool tryKeyPressGallery(QKeyEvent *event);
    bool tryKeyPressWorkspaceShear(QKeyEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);

    // wheelEvent phases
    bool tryWheelGalleryZoom(QWheelEvent *event);
    bool tryWheelGalleryScroll(QWheelEvent *event);
    void wheelZoomViewAboutCursor(QWheelEvent *event);
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    /** Forward drag/drop from the OpenGL viewport to the view handlers. */
    bool viewportEvent(QEvent *event) override;

private:
    enum LoadRole {
        LoadReplace = 0,
        LoadAdd = 1,
        LoadRestore = 2
    };

    /**
     * Create a canvas item from decoded pixels.
     * @p applyStoredSessionCrop — when true (normal disk decode), re-apply the
     * path's session crop from m_itemStates. When false, @p image is used as-is
     * (Workspace duplicate: pixels already include any crop/transform bake).
     */
    ImageItem *createItemFromImage(const QString &path, const QImage &image,
                                   bool applyStoredSessionCrop = true);
    /** Image-mode only: content appearance for a newly created canvas item. */
    WorkspaceItemState appearanceForNewImageModeItem(const QString &path);
    /** Interactive / gallery / static flags for the current ViewMode. */
    void scheduleImageLoad(const QString &path, LoadRole role);
    /**
     * If slideshow already holds pixels for @a path, queue onImageLoaded and
     * return true (skips a second disk decode under the hold).
     */
    bool tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen);
    /** Soft preview + PreferCache quality climb while slideshow is active. */
    void scheduleSlideshowReplaceDecode(const QString &path, quint64 gen, LoadRole role);
    /** Parallel thumbnail + full native decode for classic LoadReplace/LoadAdd. */
    void scheduleClassicImageDecode(const QString &path, quint64 gen, LoadRole role);
    /**
     * Image mode: install sharper ladder/PreferCache pixels in place (soft→HQ).
     * Called from onLadderReady; no-op when full native already covers.
     */
    /** SoftPreview vs FullSource from delivered sample size (not request edge). */
    SessionAppearance::PixelKind pixelKindForImageModeSample(const QString &path,
                                                             const QImage &image) const;
    ImageItem *imageModeItemForPath(const QString &path) const;
    void scheduleImageModePreferCacheClimb(const QString &path, int wantEdge = 0);
    /**
     * Debounce climb + tile tick after zoom (wheel/toolbar). Avoids per-notch
     * set_viewport/issue_requests on the GUI thread during continuous zoom.
     * Pan still ticks immediately.
     */
    void scheduleTileLodAfterInteraction(int delayMs = 50);
    /** PreferCache/display edge: min(want, ladder max, known native long edge). */
    int cappedDisplayEdgeForPath(const QString &path, int wantEdge) const;
    void installImageModeSampleInPlace(ImageItem *item, const QString &path, const QImage &image,
                                       SessionAppearance::PixelKind kind);
    /** Install/upgrade Image-mode sample; schedules PreferCache on soft. */
    /** Absolute content want for @p item (session store + live flags). */
    WorkspaceItemState wantAppearanceForItem(const ImageItem *item,
                                             SessionImageId sid = kInvalidSessionImageId) const;
    bool canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                 SessionAppearance::PixelKind kind) const;
    /** Native host + materialize for PNG/PDF/print (off-GUI load). */
    QImage blockingExportDisplayForItem(const ImageItem *item) const;
    void paintHighResExportItems(QPainter *painter, const QRectF &sourceScene,
                                 const QRectF &targetRect) const;
    DisplaySurface::State displaySurfaceStateForItem(const ImageItem *item,
                                                     int hostLongEdge = -1,
                                                     bool climbPending = false) const;
    /**
     * Execute a DisplaySurface::Action for @p item (climb / async / attach).
     * @p hostSample used for Attach*; empty → ImageCache::get(path).
     * @return true if item layout size changed after attach.
     */
    bool applyDisplaySurfaceAction(ImageItem *item,
                                   const DisplaySurface::Action &act,
                                   const QImage &hostSample,
                                   int fallbackNeedEdge,
                                   PathRasterService::ClimbPolicy climbPolicy);
    /**
     * If ImageCache has a sample with long edge ≤512, materializeDisplay
     * for @p want and attach to @p item. Returns true when attached.
     */
    /**
     * Sole attach: display pixels + layoutSize(native, want) + applied fingerprint.
     * @p display is already materializeDisplay output (or identity raw).
     */
    void attachDisplaySample(ImageItem *item, const QImage &display,
                             const WorkspaceItemState &want,
                             SessionAppearance::PixelKind kind);
    /** Host ≤512 materialize + attach; multi-MP returns false (caller schedules). */
    bool tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want);
    /**
     * Force contentRect / intrinsic from file-native + want (SIZE.md).
     * Soft sample dimensions must never redefine geometry when a durable size
     * is known. After live rotate/flip, always call this so paint does not
     * stretch oriented pixels into the pre-orient box.
     */
    void applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want);
    /** Worker materialize when host is multi-MP; attach on GUI if gen/want still match. */
    void scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                        const WorkspaceItemState &want);
    void finishAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                      const WorkspaceItemState &want, const QImage &display);
    bool sampleCoversNativeLogical(const QString &path, const QImage &image) const;
    void noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                          const QImage &sample);
    void ensureImageModeQualityClimb(const QString &path, const QImage &sample);
    /** One ImageLoader::load when thumtoo Full settles short of on-screen need. */
    void scheduleImageModeNativeDecodeOnce(const QString &path);
    bool tryInstallImageModeSample(const QString &path, const QImage &image);
    bool tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                       SessionAppearance::PixelKind kind);
    /** On-screen long edge (device px) for the current Image-mode item. */
    int imageModeOnScreenNeedEdge() const;
    /** PreferCache + native full when zoom leaves soft samples undersampled. */
    void maybeClimbImageModePixelsForView();
    void upgradeImageModeFromLadder(const QString &path, int maxEdge, const QImage &image);
    /** Gallery soft state + install path for a ladderReady delivery. */
    void applyGalleryLadderReady(const QString &path, int maxEdge, const QImage &image);
    void applyWorkspaceLadderReady(const QString &path, int maxEdge, const QImage &image);
    /** PathRaster EscalateToFull for selected Workspace items. */
    void ensureWorkspaceQualityClimb();
    void applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path);
    /** Fit / slideshow zoom / motion handoff after Image-mode full replace. */
    void frameImageModeReplaceItem(ImageItem *item, const QString &path);
    /**
     * Image mode: drop the previous frame and show a loading/provisional tile
     * for @p path immediately (do not wait for the background decode).
     */
    void installImageModePendingTile(const QString &path, const QImage &preview = QImage());
    /** Bind Image-mode item to the current session cursor (id + index). */
    void bindImageModeSessionCursor(ImageItem *item);
    /** Neutral Image-mode pose (no Workspace free-form scale/rotation). */
    void resetImageModeItemPlacement(ImageItem *item);
    /** Soft pixels for pending tile: explicit preview → slideshow map → ImageCache. */
    QImage resolveImageModePendingPixels(const QString &path, const QImage &preview) const;
    /** @p displayReadyOut set when soft is already content-baked (stashed Gallery tile). */
    QImage resolveImageModePendingPixels(const QString &path, const QImage &preview,
                                         bool *displayReadyOut) const;
    ImageItem *createPlaceholderItem(const QString &path, const QSize &intrinsicSize);
    QSize probeImageSize(const QString &path) const;
    /**
     * Best-known native pixel size for @p path from the session cache only.
     * Never does filesystem I/O — returns a neutral size and schedules an async
     * probe when unknown (slow network/USB must not block the GUI thread).
     */
    QSize imageSizeForPath(const QString &path);
    /**
     * Layout geometry for Image-mode pending tiles. Prefer cached native size;
     * if still unknown, use @p previewHint aspect so fitInView fills the window
     * (neutral 1000×1000 made portrait/landscape images look letterboxed).
     */
    QSize layoutSizeForPath(const QString &path, const QImage &previewHint = QImage());
    /**
     * Cache-only pass before Gallery pack: fill m_sizeBook from
     * ThumtooCache::cachedSize and ImageCache from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);
    bool isProvisionalImageSize(const QString &path) const;
    /** Remember native size after a successful full decode (or async probe). */
    void rememberImageSize(const QString &path, const QSize &size);
    /**
     * After a decode: prefer thumtoo/probe size. Never treat soft/ladder
     * sample dimensions as definitive logical size.
     */
    void rememberSizeFromDecode(const QString &path, const QImage &image);
    void scheduleImageSizeProbe(const QString &path);

    // --- GallerySizeResolveHost -------------------------------------------------
    bool hasDefinitiveHostSize(const QString &path) const override;
    void adoptResolvedSize(const QString &path, const QSize &size) override;
    void scheduleSizeProbe(const QString &path) override;
    QStringList sizeResolvePathOrder() const override;
    bool sizeResolveLayoutDefersPopulate() const override;
    void setSizeResolveProgress(const QString &title,
                                 const QString &detail) override;
    void clearSizeResolveProgress() override;
    void onSizeResolveGateComplete() override;
    void onSizeResolveGateCancelled() override;

    // --- TileNeighborPrefetchHost ------------------------------------------
    bool pathOnLiveCanvas(const QString &path) const override;
    QSize viewportWidgetSize() const override;
    qreal prefetchDevicePixelRatio() const override;
    bool tilePrefetchNavHot() const override;

    void applyProbedImageSize(const QString &path, const QSize &size);
    /**
     * Gallery open size probes: schedule probes for paths still missing a
     * definitive size. Returns true only when the current layout must defer
     * populate until all sizes settle (Fill / FlowFill). Grid and ordinary
     * masonry pack immediately with provisional sizes; probes still run and
     * sizeReady debounces a repack.
     */
    bool startGallerySizeResolveIfNeeded(const QStringList &paths);
    /** True when pack needs every aspect before the first layout (Fill modes). */
    static bool layoutDefersPopulateUntilSizes(LayoutMode mode);
    void noteGallerySizeProbeSettled(const QString &path);
    /** LQIP install + tile pyramid request for a Gallery path. */
    void scheduleGalleryDecode(const QString &path);

    int galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut = nullptr) const;
    /** Recover blank Gallery cells that never got LQIP installed. */
    void gallerySoftWatchdogTick();
    /** Clear stale "Improving previews" centre HUD noise. */
    void updateGallerySoftProgressHud();
    /** Slideshow phase buffers only: DisplaySurface::decide while transition live. */
    void slideshowPhaseSurfaceTick();
    void bindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id, const QString &path);
    void unbindSlideshowPhaseSurface(DisplaySurface::SurfaceId *id);
    /** Bind/sync ImageFocus surface (aliases primary item surface id). */
    void ensureImageFocusSurface();
    void syncImageFocusSurfaceState();
    void driveImageFocusSurface();
    /** Bind/unbind DisplaySurface for a canvas item (Gallery/Workspace/Image). */
    void registerItemDisplaySurface(ImageItem *item);
    void unregisterItemDisplaySurface(ImageItem *item);
    /** Push live item state into the bound surface (register if needed). */
    void syncItemDisplaySurface(ImageItem *item, int hostLongEdge = -1,
                                bool climbPending = false);
    /** Ladder step for item cell size in device pixels. */
    int itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes = true) const;
    int galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes = false) const;
    ImageItem *primaryItem() const;
    QList<ImageItem *> transformTargets() const;
    /** Apply session crop from @p state to a freshly decoded item (no-op if none). */
    /**
     * Apply stored session appearance (crop + content bakes) for @p item's
     * session id / index / unbound path. Pixels must be the full on-disk image.
     */
    void applyStoredAppearancePixels(ImageItem *item, const WorkspaceItemState &app,
                                     SessionImageId sid);
    const WorkspaceItemState *resolveStoredAppearance(ImageItem *item,
                                                       WorkspaceItemState *fallback,
                                                       SessionImageId *sidOut);
    void applyStoredAppearance(ImageItem *item);
    /**
     * After installing full on-disk pixels (soft→full), re-apply crop / content
     * flip / quarter-turn / grade from the item's session appearance without a
     * second disk load. No-op when no appearance is stored.
     */
    void applyContentAppearanceAfterDecode(ImageItem *item);
    /**
     * If @p sid has no m_appearance entry, load durable content-hash state for
     * @p path into the store (no-op when missing / thumtoo off).
     */
    void seedSessionAppearanceFromState(SessionImageId sid, const QString &path);
    void applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                          const ThumtooCache::StoredContentAppearance &stored);
    /** Record that durable appearance was probed for @p sid (hit or miss). */
    void markAppearanceSeedAttempted(SessionImageId sid);
    /**
     * Map item-local draft rect to source pixel rect of the *current* pixmap,
     * then compose into original on-disk coordinates in @p state.
     */
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);

    // CropHandle is defined in cropsession.h
    ImageItem *resolveInactiveCropTarget() const;
    ImageItem *cropTargetItem() const;
    ImageItem *cropSessionBoundItem() const;
    SessionImageId cropRecordSessionId(const ImageItem *item) const;
    void fitImageOrUpdateWorkspace(ImageItem *item);
    void ensureCropRectValid();
    QRectF cropRectItemLocal() const { return m_crop.currentRect(); }
    qreal cropRotation() const { return m_crop.currentRotation(); }
    /** Crop corners in item-local space (rotation about rect centre). */
    QRectF cropRectView() const;
    QPolygonF mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const;
    QPolygonF cropPolygonView() const;
    /** Viewport chrome button rects under the draft frame (empty when inactive). */
    CropGeometry::CropButtonLayout cropChromeLayout() const;
    bool cropAllowExpand() const { return m_crop.isAllowExpand(); }
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    QPointF itemLocalFromView(ImageItem *item, const QPoint &viewPos) const;
    void paintCropRotateAndMoveGrips(QPainter &painter, const QPolygonF &cropViewPoly);
    void paintCropFrameDecorations(QPainter &painter, const QPolygonF &cropViewPoly);
    static QString cropChromeButtonLabel(CropHandle kind);
    void paintCropChromeButton(QPainter &painter, const QRect &btn, CropHandle kind,
                               const QString &label, CropGeometry::CropBtnRole role,
                               bool toggled = false);
    void paintCropChromeButtons(QPainter &painter);
    void paintCropSizeBadge(QPainter &painter, const QRect &cropView);
    void paintCropOverlayBody(QPainter &painter, const QPolygonF &cropViewPoly,
                              const QRect &cropView);
    void paintCropOverlay(QPainter &painter);
    void paintAttentionOverlay(QPainter &painter);
    int attentionHandleIndexAt(const QPoint &viewPos) const;
    bool attentionHandleAt(const QPoint &viewPos) const;
    QVector<QPointF> attentionPointsForTarget() const;
    QPointF attentionNormForTarget() const;
    void setAttentionPointsForTarget(const QVector<QPointF> &pts);
    void setAttentionNormForTarget(const QPointF &norm);
    void ensureAttentionPoint();
    SessionImageId attentionSessionId() const;
    /** Push undo if points changed during a drag/add gesture; clear gesture state. */
    void attentionCommitSelectionMove();
    void attentionDeleteSelected();
    /** Snapshot → mutate → single undo entry (delete, detect, non-gesture edits). */
    void pushAttentionPointsUndo(const QVector<QPointF> &before,
                                 const QVector<QPointF> &after,
                                 const QString &text);
    QPointF attentionViewPos(ImageItem *item, const QPointF &norm) const;
    static void cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld);
    void beginCropHandleDrag(CropHandle h, const QPoint &viewPos);
    void updateCropHandleDrag(const QPoint &viewPos);
    void endCropHandleDrag();
    bool contentLocalContains(ImageItem *item, const QPointF &local) const;
    void finishCropRubberBand();
    void beginCropRubberBand(const QPoint &viewPos);
    void updateCropRubberBand(const QPoint &viewPos);
    void endCropRubberBand();
    void leaveCropModeInternal(bool apply);
    /** Schedule thumtoo full / pool decode while crop shows a provisional sample. */
    void onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                     quint64 gen);
    void scheduleCropFullRasterFromPool(const QString &path);
    void requestCropFullRaster(const QString &path);
    /** Upgrade crop source when native full arrives for m_crop.awaitingFullPath. */
    void acceptCropFullRasterReady(const QString &path, const QImage &image);
    void maybeUpgradeCropFullRaster(const QString &path, const QImage &image);
    void pushCropAppearanceUndo(ImageItem *item, const QString &text);
    QImage pickCropApplyAppearanceImage(ImageItem *item,
                                        const QImage &preferredDisplay) const;
    void emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                 ImageItem *item, const QImage &preferredDisplay,
                                 bool hasCrop);
    void relayoutAfterCropLeave(ImageItem *item);
    void flashCropHud(const CropFlash::Hud &hud);
    void storeCropAppearance(ImageItem *item, SessionImageId sid,
                             const WorkspaceItemState &s);
    QSize cropRecordFileNative(const QString &path) const;
    void commitCropApplyBake(ImageItem *item, const QImage &display,
                             const WorkspaceItemState &st, bool multiMp,
                             qreal cropW, qreal cropH, const QString &path,
                             const QPointF &cropSceneCenter, bool hostFromCache,
                             SessionImageId sid);
    bool bakeAndCommitNonFullApply(ImageItem *item, qreal cropW, qreal cropH,
                                   qreal footW, qreal footH,
                                   const QPointF &cropSceneCenter);
    bool applyCropCommit(ImageItem *item);
    /**
     * Image mode: keep only multiples of 90° from session state; free Workspace
     * angles map to 0°.
     */
    static qreal cardinalRotationOrZero(qreal degrees);
    /**
     * Enter crop: show full on-disk pixels with prior crop as the draft rect
     * so the region can grow. Returns false if the image cannot be prepared.
     */
    bool prepareCropModeFullImage(ImageItem *item);
    bool loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const;
    void cancelPathRasterForCrop(const QString &path);
    /** Workspace: shift item so local origin (image centre) maps to @p sceneAnchor. */
    void alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    /** Cancel path: put the session crop (if any) back on the live item. */
    bool loadPathBookAppearance(ImageItem *item, WorkspaceItemState *app) const;
    bool loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                    SessionImageId *sidOut) const;
    void rematerializeIfContentXformMismatch(ImageItem *item,
                                            const WorkspaceItemState &app);
    void installRestoredCropPixelsFromFull(ImageItem *item, const WorkspaceItemState &app,
                                           SessionImageId sid, const QImage &full);
    void installRestoredCropPixels(ImageItem *item, const WorkspaceItemState &app,
                                   SessionImageId sid, const QImage &full);
    void restoreSessionCropAppearance(ImageItem *item);
    void updateMouseInfo(const QPoint &viewPos);
    /** Frame @p item in the view. Image mode: does not clear rotation/flips. */
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    /**
     * After logical size change on the Image-mode target: keep on-screen framing
     * when aspect is stable (soft→full / probe magnitude). Refit only if aspect
     * changed or size was unknown.
     */
    void preserveImageViewOnLogicalSizeChange(ImageItem *item, const QSize &before,
                                              const QSize &after);
    /**
     * Image mode: keep sceneRect tight to the target item (small fixed margin).
     * Prevents leftover Workspace/Gallery/null scene rects from allowing free or
     * asymmetric pan when the image is fitted. No-op outside Image mode.
     */
    void syncImageModeSceneRect(ImageItem *item);
    Qt::AspectRatioMode currentFitAspectMode() const;
    void ensureVisibleItem(ImageItem *item);
    qreal angleAt(const QPointF &scenePos, ImageItem *item) const;
    void rememberItemState(ImageItem *item);
    /** Detach from view state, remove from scene, delete. Safe against drag/handle ptrs. */
    /** Expand sceneRect around free-form items so pan/scrollbars have range. */
    void restoreWorkspace();
    /** Detach Workspace tiles from the scene (keep decoded pixels + view). */
    void stashWorkspaceItems();
    /** Reattach stashed Workspace tiles; no-op if empty. */
    void restoreStashedWorkspaceItems();
    /** Detach Gallery tiles from the scene (keep decoded pixels). */
    void stashGalleryItems();
    /** Reattach stashed Gallery tiles; no-op if empty. */
    void restoreStashedGalleryItems();
    void restoreFreeFormStates();
    WorkspaceItemState defaultStateForPath(const QString &path, int ordinal) const;
    /** Centre position that does not overlap existing items (viewport-aware). */
    QPointF findEmptyPlacement(const QSizeF &itemSize) const;
    EdgeZone edgeZoneAt(const QPoint &viewPos) const;
    int edgeZoneWidth() const;
    int edgeZoneHeight() const;
    /** @return true when the hover edge zone changed. */
    bool setHoverEdge(EdgeZone zone);
    void clearHoverEdge() { (void)setHoverEdge(EdgeZone::None); }
    void updateHoverEdge(const QPoint &viewPos);
    void drawEdgeAffordances(QPainter &painter);
    QRectF selectionSceneBounds(const QList<ImageItem *> &items) const;
    void paintGroupSelectionChrome(QPainter *painter, const QList<ImageItem *> &items) const;
    int groupHandleAt(const QPoint &viewPos, const QList<ImageItem *> &items) const;
    bool beginGroupScale(int handle, const QList<ImageItem *> &items);
    void updateGroupScale(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void endGroupScale();
    /** Handles 0–7 = scale corners/edges; 8–11 = rotate (T/R/B/L). */
    bool isGroupRotateHandle(int handle) const { return handle >= 8 && handle <= 11; }
    void updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void updateGalleryHoverAt(const QPoint &viewPos);
    static QSizeF nativeSize(const ImageItem *item);
    void zoomViewBy(qreal factor);

    QGraphicsScene *m_scene = nullptr;
    /** Gallery-mode collaborator (stash, viewport snapshot, transitions). */
    GalleryController m_gallery;
    /** Workspace-mode collaborator (stash, free-form snapshot, transitions). */
    WorkspaceController m_workspace;
    /** Image-mode collaborator (enter transition). */
    ImageController m_image;
    QList<ImageItem *> m_items;
    /**
     * Path-keyed placement / legacy unbound appearance.
     * Bound session images: content appearance is m_appearance only.
     * Path map remains Workspace free-placement cache and unbound fallback.
     */
    PathItemStateBook m_itemStateBook;
    /**
     * Per-session-slot appearance (crop / content flip / quarter turns).
     * Keyed by session index so path duplicates stay independent value copies.
     */
    /** Per-session-image appearance, keyed by stable SessionImageId. */
    /** Content appearance by stable session-image id (Phase 2 store). */
    SessionAppearanceStore m_appearance;
    /** Session ids we already tried to seed from path XDG (success or miss). */
    /** Gallery tiles kept while in Image mode (decoded pixels retained). */
    /** Last setWorkspacePaths order — used to keep m_items sorted for Gallery pack. */
    /**
     * Path → native pixel size + provisional / probe-scheduled sets.
     * Dimensions are a property of the file; safe to key by path (not id).
     */
    ImageSizeBook m_sizeBook;
    /** Centre HUD progress (expand / size resolve / sort). */
    CentreProgress m_centreProgress;
    /** Gallery open: wait for sizes before creating scene tiles. */
    /** Packaged-layout size gate (timers + pending); canvas finish via Host. */
    GallerySizeResolve m_gallerySizeResolve;
    // Soft/display samples: ImageCache only (docs/PIXEL_HOST_CACHE.md).
    /** Session path list + parallel SessionImageId order (IDENTITY). */
    SessionPathOrder m_pathOrderBook;

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;

    int pageGuideHandleAt(const QPoint &viewPos) const;
    void paintPageGuideHandles(QPainter *painter) const;
    bool beginPageGuideResize(int handle);
    void updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    QRectF pageGuideRectFromHandleDrag(const QPointF &scenePos,
                                       Qt::KeyboardModifiers mods) const;
    void endPageGuideResize();

    PageGuideSession m_pageGuide;
    ViewFraming m_framing;
    ViewMode m_viewMode = ViewMode::Image;
    SessionNavFlags m_sessionNav;
    /** Gallery: path under cursor for HUD filename (empty when none). */
    /** Last mouse position in viewport coords (gallery hover + scroll). */
    CanvasBackground m_canvasBg;
    TextLayerSession m_textLayer;
    HudAppearance m_hudPrefs;
    SessionIdentity m_sessionId;
    HudFlash m_hudFlash;
    /** Persistent slideshow-paused cue (top-left); not cleared by flash timer. */
    QElapsedTimer m_lastSlideshowCenterClick;
    QTimer *m_hudFlashTimer = nullptr;
    QTimer *m_slideshowProgressTimer = nullptr;
    SlideshowProgressHud m_ssHud;
    SlideshowSettings m_ssSettings;
    /** Pure-phase composite (from/to buffers, clocks). */
    SlideshowPhaseState m_ss;
    /** Dwell atlas + Ken Burns camera (timer stays below). */
    SlideshowDwellState m_ssDwell;
    // Slideshow samples: ImageCache only (putSlideshowRaster / slideshowRaster).
    /** ZoomBlur letterbox underlay cache (two slots). */
    mutable SlideshowZoomBlurState m_ssZoomBlur;
    /** Scroll policies restored when Ken Burns underlay returns. */
    MotionScrollChrome m_motionScroll;
    QTimer *m_motionTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutPrefs m_layout;
    SessionLoadGate m_loadGate;
    GallerySoftBook m_gallerySoftBook;
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    PathRasterService *m_pathRaster = nullptr;
    std::unique_ptr<TileLoadCoordinator> m_tileCoordinator;
    void gallerySoftResetPath(const QString &path);
    void gallerySoftResetAll();
    /** @deprecated Gallery always virtualizes; kept for ABI/docs only. */
    static constexpr int kGalleryVirtualThreshold = 1;
    /** Historical Gallery concurrency knob (unused after soft removal). */
    static constexpr int kMaxConcurrentGalleryDecodes = 8;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    /** Queue of workspace restores still waiting for decode (supports same path twice). */
    /** Optional scene centre for in-flight LoadAdd decodes (e.g. drops). */
    // --- LoadAdd pending-bind helpers ---
    int countPendingSessionBinds(const QString &path) const;
    void purgeSatisfiedPendingBinds(const QString &path);
    bool takePendingSessionBind(const QString &path, PendingSessionBind *out);
    void applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound);
    bool installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image);
    bool takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                          PendingSessionBind *out);
    void placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                             bool haveBound, const PendingSessionBind &bound);

    SessionBindBook m_bindBook;
    /** Content appearance staged by Duplicate until bindSelectedSessionIds. */
    PendingItemAppearanceBook m_pendingAppearance;
    ViewportChrome m_chrome;
    /** One-shot rubber-band zoom (Z). */
    ZoomRegionGesture m_zoomRegion;

    ItemInteractSession m_itemInteract;

    CropSession m_crop;
    AttentionSession m_attention;
    /** Image-mode focus surface (DisplaySurfaceController). Invalid outside Image. */
    DisplaySurfaceController m_displaySurfaces;
    DisplaySurface::SurfaceId m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;

    /** Multi-select group scale/rotate gesture (Workspace). */
    GroupTransformSession m_groupXform;

    LayoutApplyGuard m_layoutApply;
    /** Nested suppress: Gallery delete must not repack via resizeEvent. */
    GalleryRelayoutSuppress m_galleryRelayoutSuppress;
    QTimer *m_galleryDecodeScrollTimer = nullptr;
    QTimer *m_galleryStatusRefreshTimer = nullptr;
    QTimer *m_statusRefreshTimer = nullptr;
    /** Pump tile LOD while zoomed (Image mode). */
    QTimer *m_tileLodTimer = nullptr;
    /** Single-shot: coalesce zoom notches before climb/tick. */
    QTimer *m_tileLodZoomDebounce = nullptr;
    /** Off-canvas neighbor tile prefetch (session-replace clears). */
    TileNeighborPrefetch m_tileNeighborPrefetch;
    /** Paths for which PreferCache was cancelled after entering tile band. */
    /** BILTOO_PERF / THUMTOO_DEBUG: paint + decode-window timings. */
    PerfStats m_perf;

    QTimer *m_gallerySoftWatchdog = nullptr;
    QTimer *m_layoutDebounceTimer = nullptr;
    LayoutDebounce m_layoutDebounce;
    /** Debounce colour-slider durable write + filmstrip (see setTargetColorAdjustments). */
    QTimer *m_colorAdjustCommitTimer = nullptr;
    ColorAdjustCommit m_colorAdjustCommit;
};

#endif // IMAGEVIEW_H
