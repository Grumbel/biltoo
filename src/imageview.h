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
#include "packorderview.h"
#include "thumtoocache.h"
#include "coloradjust.h"
#include "sessionappearance.h"
#include "itemworld.h"
#include "sessiondocument.h"
#include "gallerycontroller.h"
#include "slideshowcontroller.h"
#include "cropcontroller.h"
#include "attentioncontroller.h"
#include "displaypipelinecontroller.h"
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
    bool addImage(const QString &path);
    /** Add (or select) the canvas instance bound to @p sessionIndex. */
    bool addImageForSession(const QString &path, int sessionIndex);
    bool addImageForSession(const QString &path, SessionImageId sessionId, int sessionIndex);
    /**
     * Workspace: place @p path at @p scenePos. If the path is already on the
     * canvas, a new instance is created (duplicate) — the original is not moved.
     */
    bool placeOrMoveImageAt(const QString &path, const QPointF &scenePos);
    bool placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                            SessionImageId sessionId, int sessionIndex);
    QList<int> selectedSessionIndices() const;
    void selectBySessionIndices(const QList<int> &indices);
    /** SessionImageIds of selected canvas items (skips unbound). */
    QList<SessionImageId> selectedSessionIds() const;
    void selectBySessionIds(const QList<SessionImageId> &ids);
    /** Select live tiles matching @p paths by occurrence order (duplicate-safe). */
    void selectPathsByOccurrence(const QStringList &paths);
    void rebindWorkspaceSession(const QStringList &sessionFiles,
                                const QVector<SessionImageId> &sessionIds);
    void clearWorkspace();
    // Load roles (public for DisplayPipelineController; not under public slots — moc).
    enum LoadRole {
        LoadReplace = 0,
        LoadAdd = 1,
        LoadRestore = 2
    };

    bool isMultiItemMode() const { return m_viewMode != ViewMode::Image; }
    /** Gallery empty-canvas press: rubber-band via QGraphicsView base. */
    void forwardGraphicsViewMousePress(QMouseEvent *event)
    { QGraphicsView::mousePressEvent(event); }
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
    /** Controller host: Workspace/Gallery rubber-band vs pan drag mode from tool. */
    void applyToolDragMode();
    /** Decode path off the GUI thread into the unified slideshow raster map. */
    /**
     * Logical image size for @a path (never soft-raster dimensions).
     * Lookup only: m_sizeBook, then thumtoo cache. Empty if unknown.
     * Slideshow and Image-mode framing share this.
     */
    QSize logicalSizeForPath(const QString &path) const override;
    /**
     * Image→viewport scale for current slideshowZoom (Fit/Fill/Actual)
     * given logical size and viewport. Pure function of size model.
     */
    /**
     * User is rapidly flipping (←/→ key-repeat) in Image mode or slideshow.
     * Suppresses PreferCache climb, sync repaint, new ZoomBlur builds, and
     * atlas work until settle; previous underlay is kept until replacement.
     */
    bool slideshowNavHot() const { return m_slideshow.hud().isNavHot(); }
    /** Slideshow pure-phase owns viewport — tile coordinator must not issue. */
    bool isSlideshowProgressActive() const { return m_slideshow.hud().isProgressActive(); }
    /** PathRasterService for PreferCache cancel when tiles issue (coordinator). */
    PathRasterService *pathRasterForCoordinator() { return m_pathRaster; }
#include "imageview_host_accessors.inc"

    /** Display pipeline host: crop controller (draft freeze). */
    CropController &hostCrop() { return m_cropCtrl; }
    const CropController &hostCrop() const { return m_cropCtrl; }
    /** Display pipeline host: text layer (loadImage side effects). */
    TextLayerSession &hostTextLayer() { return m_textLayer; }
    const TextLayerSession &hostTextLayer() const { return m_textLayer; }
    /** Display pipeline host: filmstrip soft provider for image-mode pending. */
    ImageModeSoftProvider hostImageModeSoftProvider() const { return m_imageModeSoftProvider; }
    /**
     * Warm overview tiles into the process-wide path registry for off-canvas
     * paths (Image-mode ±1 neighbors after nav settle). Controllers stay alive
     * and are pumped on a short timer until coverage or a tick budget expires;
     * release then leaves Succeeded tiles in the registry (1212 retain).
     * No-op while nav-hot; schedules tile pyramid when durable unknown.
     */
    void prefetchTilesForPaths(const QStringList &paths, int budgetPerPath = 4);

    /** Controller host: session path order used for Gallery packing. */
    void clearPathOrder() { pathOrderClear(); }
    /** Replace pack order (paths ∥ ids). Prefer PackOrderView overload. */
    void setPathOrder(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        pathOrderSetOrder(paths, ids);
    }
    void setPathOrder(const PackOrderView &pack)
    {
        pathOrderSetOrder(pack.paths(), pack.ids());
    }
    /** Rebuild pack order from live tiles (path∥sessionId). Id-safe. */
    void setPathOrderFromLiveItems();
    /** Controller host: disable Image-mode fit/fill when restoring free-form. */
    void clearFitFillModes();
    /** Controller host: Image/Gallery soft reset to fit, not fill. */
    void enableFitMode();
    /** Controller host: session appearance store (id-keyed). */
    /**
     * Session appearance (id-keyed). Always the SessionDocument store after
     * Phase 6 Tier 4b — call bindSessionAppearance before any appearance use.
     */
    SessionAppearanceStore &appearance()
    {
        return m_itemWorld.appearance();
    }
    const SessionAppearanceStore &appearance() const
    {
        return m_itemWorld.appearance();
    }
    /** Phase 7 Stage 0: facade over appearance / path-book / size-book. */
    ItemWorld &itemWorld() { return m_itemWorld; }
    const ItemWorld &itemWorld() const { return m_itemWorld; }
    /** Point at SessionDocument::appearance() (required before appearance()). */
    void bindSessionAppearance(SessionAppearanceStore *store)
    {
        m_appearanceBound = store;
        m_itemWorld.bindAppearance(store);
    }
    /**
     * Phase 6 Tier 4 path-order: MainWindow binds the working SessionDocument.
     * firstSessionIdForPath uses the document; LoadAdd multiplicity stays on the
     * view path-order book (clearPathOrder must not consult the document).
     */
    void bindSessionDocument(SessionDocument *doc)
    {
        m_sessionDoc = doc;
    }
    /** Controller host: path-keyed placement / unbound appearance cache. */
    void setItemStateForPath(const QString &path, const WorkspaceItemState &state)
    {
        m_itemWorld.setPathState(path, state);
    }

    const WorkspaceItemState *itemStateForPath(const QString &path) const
    {
        return m_itemWorld.getPathState(path);
    }
    bool hasPendingWorkspacePaths() const { return m_displayPipeline.loadGate().hasPendingWorkspacePaths(); }
    void clearPendingWorkspacePaths() { m_displayPipeline.loadGate().clearPendingWorkspacePaths(); }
    void addPendingWorkspacePath(const QString &path) { m_displayPipeline.loadGate().addPendingWorkspacePath(path); }
    void takePendingWorkspacePath(const QString &path);
    void setPendingRestoreStates(const QList<WorkspaceItemState> &states)
    {
        m_displayPipeline.loadGate().setPendingRestoreStates(states);
    }



#include "imageview_host_crop_display.inc"

#include "imageview_host_ops.inc"

    void setViewMode(ViewMode mode);

    /**
     * Gallery or Workspace → Image (Phase 3).
     * Gallery: snapshot viewport then stash tiles. Workspace: durable snapshot
     * + stash (inside setViewMode). Caller loads the focused path afterward.
     */
    void leaveForImageMode();

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
    bool isImageMode() const { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const { return m_viewMode == ViewMode::Workspace; }

    /** @deprecated Alias for isWorkspaceMode(). */
    bool workspaceMode() const { return isWorkspaceMode(); }

    /**
     * Enable left/right edge navigation affordances in Image mode.
     * Typically true when the session has more than one image.
     */
    void setImageModeNavigationEnabled(bool on);
    /** When true, Image mode top edge offers return-to-gallery. */
    void setGalleryReturnAvailable(bool on);

    /**
     * Show exactly the given paths on the workspace. Images already present
     * keep their live transform; newly added ones restore saved state or get
     * a default placement. Images no longer listed are removed from the
     * scene after their state is saved.
     */
    void setWorkspacePaths(const QStringList &paths);
    void setWorkspacePaths(const QStringList &paths,
                           const QVector<SessionImageId> &sessionIds);
    /** Reorder canvas items to match @p paths (session / sort order). */
    void reorderItemsByPaths(const QStringList &paths);

    /**
     * While true, Gallery ignores resize/debounce-driven applyLayout (used
     * during session delete so the pack and scroll stay put).
     */
    void setGalleryRelayoutSuppressed(bool on);

    /**
     * Reload from disk: Image mode — current session image only;
     * Gallery — re-decode all tiles (keeps positions unless @p relayout);
     * Workspace — re-decode on-canvas items in place.
     */
    void reloadFromDisk(bool relayoutGallery = true);
    /**
     * Hard reload (Shift+F5): drop host ImageCache + tile path RAM + thumtoo
     * settled-pixel markers for the target paths, clear decoded pixels, then
     * re-decode from disk. Image mode — current image; Gallery/Workspace —
     * selection if any, otherwise all on-canvas items.
     */
    void hardReloadFromDisk(bool relayoutGallery = true);
    /** When true, destroyCanvasItem does not clear the undo stack (session remove). */
    void setPreserveUndoOnDestroy(bool on) { m_preserveUndoOnDestroy = on; }
    /**
     * Gallery: scroll/HUD focus for session cursor without collapsing multi-select.
     * (focusSessionPath exclusive-selects — for keyboard nav.)
     */
    void revealGalleryPath(const QString &path);
    ImageItem *findItemBySessionId(SessionImageId sessionId) const;
    /** True while LoadAdd still has an unbound PendingSessionBind for @p path. */
    bool hasPendingSessionBindForPath(const QString &path) const;
    void removeWorkspaceSessionId(SessionImageId sessionId);
    /** Assign sequential session indices to currently selected items starting at @p first. */
    void bindSelectedSessionIndices(int firstSessionIndex);
    /** Bind selected canvas items to stable session ids (same order). */
    void bindSelectedSessionIds(const QList<SessionImageId> &ids);
    /** How many canvas items currently show @p path. */
    int workspacePathOccurrenceCount(const QString &path) const;

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
    bool stickyZoomEnabled() const { return m_framing.isStickyZoomEnabled(); }
    void setStickyZoomKind(StickyZoomKind kind);
    StickyZoomKind stickyZoomKind() const { return m_framing.currentStickyZoomKind(); }
    /**
     * One-shot rubber-band zoom: next left-drag selects a region to zoom into.
     * Esc cancels. Bound to Z from the main window.
     */
    void armZoomRegion();
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
     * Attention / focus-point mode (Image mode). Multi-point overlay with
     * standard selection: click select, Shift/Ctrl toggle, drag empty =
     * rubber-band, Ctrl+click = insert, Del = delete, undoable edits.
     * Primary (first) point drives slideshow Ken Burns. Auto-detects peaks
     * when entering if none stored.
     */
    void setAttentionMode(bool on);
    bool isAttentionMode() const { return m_attentionCtrl.active(); }
    void toggleAttentionMode();
    /** Restore pixels + session crop metadata (used by crop undo/redo). */
    void applyCropAppearance(ImageItem *item, const QImage &src,
                            const WorkspaceItemState &state);

    /** When true (default), left-drag pans in Image mode. */
    void setImageModeLeftDragPan(bool on);

    /** Debug: paint text/link region rects for page documents (Image mode). */
    void setShowTextRegions(bool on);

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
    /** Copy selected text to the clipboard; returns false if nothing selected. */
    bool copySelectedText();
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
     * Session Gallery / Image canvas override (not Preferences, not project).
     * AppDefault follows Preferences materials. Shared across Gallery and Image.
     */
    void setViewBackground(const WorkspaceBackground &bg);
    WorkspaceBackground viewBackground() const { return m_canvasBg.viewRef(); }

    /**
     * Session position for status line and HUD (index/total, 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     * @p pulseIdentity briefly shows filename/badge without a pinned HUD (H);
     * use false for silent updates (e.g. slideshow auto-advance).
     */
    void setCurrentSessionId(SessionImageId id);
    SessionImageId currentSessionId() const { return m_sessionId.currentIdValue(); }
    /** Select canvas item for @p path; ensure visible in Gallery. */
    void focusSessionPath(const QString &path);
    int sessionIndex() const { return m_sessionId.currentIndex(); }

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    bool hudVisible() const { return m_hudPrefs.isVisible(); }
    /** Corner marks for crop / orient / grade (default on). */
    void setContentEditMarksVisible(bool on);
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
    /**
     * Overall slideshow timeline for the extended (pinned) HUD — video-player
     * style elapsed / total and remaining. Pass totalMs<=0 to clear.
     * Drawn only while the full HUD is pinned and a slideshow is active.
     */
    /** Per-cycle phase in [0,1] from host unitless clock. */

    SlideshowTransition slideshowTransition() const { return m_slideshow.settings().currentTransition(); }
    int slideshowTransitionDurationMs() const { return m_slideshow.settings().transitionDuration(); }
    /** Clear residual transition overlay state (safe during pure-phase show). */

    SlideshowMotion slideshowMotion() const { return m_slideshow.settings().currentMotion(); }
    qreal panZoomFactor() const { return m_slideshow.settings().currentPanZoomFactor(); }

    SlideshowZoom slideshowZoom() const { return m_slideshow.settings().currentZoom(); }

    SlideshowLetterboxFill slideshowLetterboxFill() const { return m_slideshow.settings().currentLetterboxFill(); }
    /** Pad colour when letterbox fill is Solid (also fallback for ZoomBlur miss). */
    /**
     * Freeze or continue Ken Burns without tearing down the dwell camera.
     * Used for slideshow pause/resume (Space), not full stop.
     */
    /**
     * Persistent top-left "Paused" cue while a slideshow session is paused.
     * Independent of flashHud (which times out after ~1s). Cleared on resume/stop.
     */
    /** Freeze/resume dwell progress elapsed without resetting the timeline. */
    /** Image-mode fit after leaving slideshow (Fit to window). */
    /**
     * Re-frame the current Image-mode item for an active slideshow:
     * motion on → restart Ken Burns blit from slideshow zoom base; motion off →
     * Fit / Fill / 1:1 framing only. No-op when slideshow progress is inactive.
     */
    /**
     * Drop any held live-transition overlay once the next slide is fitted.
     * Called from the LoadReplace path so the incoming frame is not cleared
     * before the new item is on screen (avoids a one-frame flash of the old image).
     */

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
    void applyLayout(GalleryPackReason reason = GalleryPackReason::ExplicitLayout);
    /**
     * Workspace only: pack @p items (or current selection) with a packaged
     * layout without leaving FreeForm / Workspace mode. Returns false if
     * there is nothing to arrange.
     */
    bool layoutWorkspaceItems(const GalleryLayout::Params &params,
                              const QList<ImageItem *> &items = {});
    void applyPendingGalleryRestore();
    GalleryLayout::Mode galleryLayoutModeFromViewMode() const;
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
    /** Stage 2: apply Workspace pose only (Placement component). */
    /** Stage 2: read live ImageItem pose into a Placement record. */
    static ItemComponents::Placement placementFromItem(const ImageItem *item);
    /**
     * Apply pose (and session DTO) after geometry undo/redo so ItemWorld
     * Placement stays aligned with the live item.
     */
    void applyGeometrySessionState(ImageItem *item, const WorkspaceItemState &state);
    /** Write Placement/appearance (or path book) without touching the live item. */
    void persistGeometrySessionState(ImageItem *item, const WorkspaceItemState &state);
    /** Persist session state and refresh filmstrip (chrome / toolbar edits). */
    void commitItemSessionEdit(ImageItem *item);
    /** Copy of stored appearance for @p id (empty/default if none). */
    WorkspaceItemState sessionAppearanceValue(SessionImageId id) const;
    bool hasSessionAppearance(SessionImageId id) const;
    /** Restore appearance after session undo (store only; no canvas mutate). */
    void setSessionAppearance(SessionImageId id, const WorkspaceItemState &state);
    void copySessionAppearance(SessionImageId fromId, SessionImageId toId);
    void setTargetColorAdjustments(const ColorAdjustments &adj);
    /** Bake flip into pixels and session state. */
    void bakeItemFlip(ImageItem *item, bool horizontal, bool vertical);

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
    /** Path of the last failed Image-mode decode (empty if none). */
    QString lastLoadError() const { return m_sessionId.lastLoadErrorRef(); }
    QSize imageSize() const;
    int itemCount() const;
    /** Live tiles, stashed tiles, or durable snapshot — Workspace is non-empty. */
    bool hasWorkspaceContent() const;
    QStringList itemPaths() const;
    /** Paths of selected canvas items (Gallery/Workspace). Image mode: current path. */
    QStringList selectedPaths() const;
    /** Select every live canvas tile (Gallery / Workspace). No-op in Image mode. */
    void selectAllCanvasItems();
    /** In-flight LoadAdd / LoadRestore / viewport-window decodes. */
    int pendingDecodeCount() const;
    /** Coalesce decode-window rescans (setInterest + schedule) off the hot path. */
    void scheduleGalleryDecodeWindowRefresh(int delayMs = 48);
    void updateGalleryDecodeWindow();
    void updateGallerySoftProgressHud();
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
public:
    /** True while @p gen is still the active LoadReplace generation (pool jobs). */
    bool matchesLoadGeneration(quint64 gen) const
    {
        return m_displayPipeline.loadGate().accepts(gen);
    }

    // Host pipeline + try* phase decls — must NOT sit under public slots (moc).
#include "imageview_host_pipeline.inc"

protected:
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
    // Phase 6 Tier 0: privatized methods — see REFACTOR.md / imageview_private_methods.inc
#include "imageview_private_methods.inc"
#include "imageview_private_rest.inc"
};
#endif // IMAGEVIEW_H
