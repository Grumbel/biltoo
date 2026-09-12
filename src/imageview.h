// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include "imageview_types.h"
#include "thumtoocache.h"
#include "coloradjust.h"
#include "sessionappearance.h"
#include "gallerycontroller.h"
#include "workspacecontroller.h"
#include "imagecontroller.h"
#include "gallerylayout.h"

#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QHash>
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
class ImageView : public QGraphicsView
{
    Q_OBJECT


public:
    enum class Tool {
        Select,
        Pan,
        Zoom /**< Workspace: rubber-band zoom to region */
    };

    enum class BackgroundPattern {
        Solid,
        Checkerboard
    };

    /** Slideshow advance transition (Image mode only). */
    enum class SlideshowTransition {
        None = 0,
        Crossfade = 1,
        FadeBlack = 2,
        /** Old frame exits left; new frame enters from the right (slide projector). */
        Slide = 3
    };

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
    enum class LayoutMode {
        FreeForm,
        SideBySide, // horizontal strip (UI: "Horizontal")
        Vertical,   // vertical strip
        Grid,
        /** Square cells; image scaled to cover and centre-cropped (like thumb crop). */
        GridCrop,
        /** Column masonry: N columns spanning the view width; variable row heights. */
        Masonry,
        /** Row masonry: N rows spanning the view height; variable column widths. */
        MasonryRows,
        /** Column masonry scaled per-column to a shared bottom edge (no dangling). */
        MasonryFill,
        /** Row masonry scaled per-row to a shared right edge (no dangling). */
        MasonryRowsFill,
        /** Session order wrap (book/comic contact sheet). */
        Flow,
        /** Flow with each row scaled to full layout width. */
        FlowFill,
        /** Two-up spreads; cover page alone, then pairs. */
        Facing
    };

    explicit ImageView(QWidget *parent = nullptr);
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
    /** Controller host: set m_viewMode + m_layoutMode and refresh viewport. */
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
    void startSlideshowMotion(int durationMs, qreal initialProgress = 0.0);
    void pickInterestingMotionBiases(uint seed, const QImage &source = QImage());
    /** Decode path off the GUI thread into the unified slideshow raster map. */
    void preloadSlideshowImage(const QString &path);
    /**
     * Slideshow decode / atlas target long-edge: viewport × DPR × motion
     * headroom (Ken Burns can zoom past 1:1 cover), ladder-snapped, capped
     * at kImageLadderEdge. Not native max.
     */
    int slideshowTargetEdge() const;
    /** ≥1: panZoomFactor or pan-scan margin so zoomed frames stay sharp. */
    qreal slideshowMotionHeadroom() const;
    /**
     * User is rapidly flipping (←/→ key-repeat). Suppresses *new* ZoomBlur
     * builds only; previous underlay is kept until a replacement is ready.
     */
    void setSlideshowNavHot(bool hot);
    bool slideshowNavHot() const { return m_slideshowNavHot; }
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
    void paintMotionCover(QPainter *painter, const QImage &image, qreal motionT,
                          QPointF biasA, QPointF biasB, uint pathHash) const;
    /**
     * Draw cover-scaled blurred @a image into @a viewportRect (ZoomBlur fill).
     * @a stableKey must identify the slide for the current viewport size
     * (path hash + dimensions). Do not use QImage::constBits() — buffers are
     * often reallocated while pixels stay the same, which forced a full CPU
     * blur every paint and spiked transitions.
     */
    void scheduleZoomBlurBuild(const QImage &image, int vw, int vh, qint64 key) const;
    void invalidateZoomBlurQueue() const;
    void paintZoomBlurUnderlay(QPainter *painter, const QImage &image,
                               const QRect &viewportRect, qint64 stableKey) const;

    /**
     * Build/refresh motion atlas: sized from viewport × motion headroom
     * (resolution-invariant), not from source pixel dimensions.
     */
    void ensureMotionAtlas(const QImage &image, QPixmap *atlas, qreal *atlasScale,
                           int *atlasVw, int *atlasVh) const;
    /** Store raster if better than what we have (larger long edge). */
    void putSlideshowRaster(const QString &path, const QImage &image);
    /** Best unoriented raster for path, or null. */
    QImage slideshowRaster(const QString &path) const;
    void setSlideshowUnderlayVisible(bool visible);
    void hideSlideshowUnderlay();
    /** Fit / Fill / 1:1 framing for a slideshow slide (motion off). */
    void applySlideshowZoomFraming(ImageItem *item);
    /** Controller host: session path order used for Gallery packing. */
    QStringList &pathOrder() { return m_pathOrder; }
    const QStringList &pathOrder() const { return m_pathOrder; }
    QVector<SessionImageId> &sessionIdOrder() { return m_sessionIdOrder; }
    const QVector<SessionImageId> &sessionIdOrder() const { return m_sessionIdOrder; }
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
    QHash<QString, WorkspaceItemState> &itemStates() { return m_itemStates; }
    const QHash<QString, WorkspaceItemState> &itemStates() const { return m_itemStates; }
    bool hasPendingWorkspacePaths() const { return !m_pendingWorkspacePaths.isEmpty(); }
    void clearPendingWorkspacePaths() { m_pendingWorkspacePaths.clear(); }
    void addPendingWorkspacePath(const QString &path) { m_pendingWorkspacePaths[path] += 1; }
    void takePendingWorkspacePath(const QString &path);
    QList<WorkspaceItemState> &pendingRestoreStates() { return m_pendingRestoreStates; }
    const QList<WorkspaceItemState> &pendingRestoreStates() const { return m_pendingRestoreStates; }

    // --- Controller host operations (mode controllers; prefer these over friend) ---
    /** Apply interactive/gallery/static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item);
    /** @deprecated Path is not identity; prefer findItemBySessionId. */
    ImageItem *findItemByPath(const QString &path) const;
    /**
     * Resolve a path to a live item without always taking the first match.
     * Prefer a selected item with that path, else the sole live match.
     * Returns nullptr when ambiguous (multiple unselected matches) or none.
     */
    ImageItem *findPreferredItemForPath(const QString &path) const;
    ImageItem *targetItem() const;
    void applySessionCrop(ImageItem *item, const WorkspaceItemState &state);
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
    bool pageGuideVisible() const { return m_pageGuideVisible; }
    /** Update guide size from a printer page layout (millimetres → scene px). */
    void setPageGuideFromPrinter(const QPrinter &printer);
    /** Size page guide to live content bounds (+ margin); shows the guide. */
    void fitPageGuideToContent(qreal marginPx = 16.0);
    /** Scene rectangle of the page guide (printer: centred; fit-content: content AABB). */
    QRectF pageGuideSceneRect() const;
    bool pageGuideSelected() const { return m_pageGuideSelected; }
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
    bool galleryReturnAvailable() const { return m_galleryReturnAvailable; }
    bool imageModeNavigationEnabled() const { return m_imageModeNavEnabled; }

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
    void setWorkspacePaths(const QStringList &paths,
                           const QVector<SessionImageId> &sessionIds);
    /** Reorder canvas items to match @p paths (session / sort order). */
    void reorderItemsByPaths(const QStringList &paths);

    /**
     * While true, Gallery ignores resize/debounce-driven applyLayout (used
     * during session delete so the pack and scroll stay put).
     */
    void setGalleryRelayoutSuppressed(bool on);
    bool galleryRelayoutSuppressed() const { return m_galleryRelayoutSuppressCount > 0; }

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
     * One-shot rubber-band zoom: next left-drag selects a region to zoom into.
     * Esc cancels. Bound to Z from the main window.
     */
    void armZoomRegion();
    void cancelZoomRegion();
    bool zoomRegionArmed() const { return m_zoomRegionArmed; }
    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;
    void rotateLeft();
    void rotateRight();
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
    bool isCropMode() const { return m_cropMode; }
    void toggleCropMode();

    /**
     * Attention / focus-point mode (Image mode). Multi-point overlay: add,
     * select (shift / rubber-band), move, delete. Primary (first) point drives
     * slideshow Ken Burns. Auto-detects peaks when entering if none stored.
     */
    void setAttentionMode(bool on);
    bool isAttentionMode() const { return m_attentionMode; }
    void toggleAttentionMode();
    /** Re-run saliency detect on the current image (replaces all points). */
    void detectAttentionPoint();
    /** Commit the draft crop rect to pixels and leave crop mode. */
    void applyCrop();
    /** Shrink draft to non-background content (margin trim). */
    void applyAutoCrop();
    /** Discard the draft and leave crop mode. */
    void cancelCrop();
    /** Restore pixels + session crop metadata (used by crop undo/redo). */
    void applyCropAppearance(ImageItem *item, const QImage &src,
                            const WorkspaceItemState &state);

    /** When true (default), left-drag pans in Image mode. */
    void setImageModeLeftDragPan(bool on);

    /** Debug: paint text/link region rects for page documents (Image mode). */
    void setShowTextRegions(bool on);
    bool showTextRegions() const { return m_showTextRegions; }
    void refreshTextLayer();

    /**
     * Highlight regions whose text contains @p query (case-insensitive).
     * Empty query clears highlights. Loads the text layer if needed.
     * Returns number of matching regions on the current page.
     */
    int setTextSearchQuery(const QString &query);
    QString textSearchQuery() const { return m_textSearchQuery; }
    int textSearchMatchCount() const { return m_textSearchMatches.size(); }
    bool hasTextLayer() const;
    int textLayerRegionCount() const;
    /** Soft match for OCR noise (alnum-only + light edit distance). Default on. */
    void setTextSearchFuzzy(bool on);
    bool textSearchFuzzy() const { return m_textSearchFuzzy; }
    /** True if @p regionText matches @p query under the same rules as Find. */
    static bool textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy);

    /** Selected region indices from Shift+drag rubber-band (Image mode page docs). */
    int textSelectionCount() const { return m_textSelectedRegions.size(); }
    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;
    /** Copy selected text to the clipboard; returns false if nothing selected. */
    bool copySelectedText();
    void clearTextSelection();
    /** Non-empty while the pointer is over a link region. */
    QString linkHoverTip() const { return m_linkHoverTip; }

    bool imageModeLeftDragPan() const { return m_imageModeLeftDragPan; }

    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_bgColor; }
    /**
     * Effective solid pad colour for slideshow letterbox (Solid mode colour,
     * else Preferences background). Used when ZoomBlur cannot run.
     */
    QColor slideshowPadColor() const;
    void setBackgroundColorAlt(const QColor &color);
    QColor backgroundColorAlt() const { return m_bgColorAlt; }
    void setBackgroundPattern(BackgroundPattern pattern);
    BackgroundPattern backgroundPattern() const { return m_bgPattern; }
    /** When true, checkerboard is used only in Workspace; other modes stay solid. */
    void setCheckerboardWorkspaceOnly(bool on);
    bool checkerboardWorkspaceOnly() const { return m_bgCheckerWorkspaceOnly; }

    /**
     * Per-Workspace background override (project state). AppDefault uses the
     * preference colours/pattern (technical default) instead of a custom look.
     */
    void setWorkspaceBackground(const WorkspaceBackground &bg);
    WorkspaceBackground workspaceBackground() const { return m_workspaceBackground; }
    void clearWorkspaceBackground(); /**< AppDefault */
    /**
     * Temporary view of the Preferences / technical background without changing
     * the project WorkspaceBackground override. Used by the Background Default
     * toolbar toggle.
     */
    void setWorkspaceBackgroundShowDefault(bool on);
    bool workspaceBackgroundShowDefault() const { return m_workspaceBackgroundShowDefault; }

    /**
     * Session position for status line and HUD (index/total, 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     * @p pulseIdentity briefly shows filename/badge without a pinned HUD (H);
     * use false for silent updates (e.g. slideshow auto-advance).
     */
    void setSessionPosition(int index, int total, bool pulseIdentity = true);
    void setCurrentSessionId(SessionImageId id);
    SessionImageId currentSessionId() const { return m_currentSessionId; }
    /** Select canvas item for @p path; ensure visible in Gallery. */
    void focusSessionPath(const QString &path);
    int sessionIndex() const { return m_sessionIndex; }
    int sessionTotal() const { return m_sessionTotal; }

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    bool hudVisible() const { return m_hudVisible; }
    void setHudFontPointSize(int pt);
    int hudFontPointSize() const { return m_hudFontPointSize; }
    void setHudTextColor(const QColor &color);
    QColor hudTextColor() const { return m_hudTextColor; }
    void setHudPanelColor(const QColor &color);
    QColor hudPanelColor() const { return m_hudPanelColor; }

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

    void setSlideshowTransition(SlideshowTransition kind);
    SlideshowTransition slideshowTransition() const { return m_slideshowTransition; }
    void setSlideshowTransitionDurationMs(int ms);
    int slideshowTransitionDurationMs() const { return m_slideshowTransitionDurationMs; }
    /** Clear residual transition overlay state (safe during pure-phase show). */
    void cancelSlideshowTransition();

    /** Image motion (Ken Burns / pan-scan) during a slideshow dwell. */
    enum class SlideshowMotion {
        Off = 0,
        PanZoom = 1, /**< Cover frame, slowly zoom in while panning */
        PanScan = 2  /**< Cover frame, pan across full width/height (no zoom) */
    };

    void setSlideshowMotion(SlideshowMotion mode);
    SlideshowMotion slideshowMotion() const { return m_slideshowMotion; }
    void setPanZoomFactor(qreal factor);
    qreal panZoomFactor() const { return m_panZoomFactor; }

    /** Base framing for each slide (static when motion off; Ken Burns base when on). */
    enum class SlideshowZoom {
        Fit = 0,    /**< Letterbox — whole image visible */
        Fill = 1,   /**< Cover — may crop */
        Actual = 2  /**< 1:1 pixels, centred */
    };
    void setSlideshowZoom(SlideshowZoom mode);
    SlideshowZoom slideshowZoom() const { return m_slideshowZoom; }

    /**
     * How to fill viewport regions the slide does not cover (Fit / Actual /
     * motion frames with bars). Orthogonal to SlideshowZoom.
     */
    enum class SlideshowLetterboxFill {
        AppBackground = 0, /**< Preferences canvas colour / checker */
        Solid = 1,         /**< Dedicated slideshow pad colour */
        ZoomBlur = 2       /**< Cover-scale + blur of current slide under sharp image */
    };
    void setSlideshowLetterboxFill(SlideshowLetterboxFill mode);
    SlideshowLetterboxFill slideshowLetterboxFill() const { return m_slideshowLetterboxFill; }
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
    bool slideshowPausedHud() const { return m_slideshowPausedHud; }
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
    /** Pure-clock drive: fadeT<0 dwell on fromPath; else crossfade from→to at fadeT in [0,1]. */
    void setSlideshowPhase(const QString &fromPath, const QString &toPath, qreal fadeT);
    QImage slideshowPixelsForPath(const QString &path);
    QImage slideshowFullIfReady(const QString &path) const;
    QImage slideshowSoftPlaceholder(const QString &path);
    /** SessionImageId for a session path (path order), or invalid. */
    SessionImageId sessionIdForPath(const QString &path) const;
    /**
     * Apply path-keyed content appearance (flip / quarter-turns / grade) to
     * unbaked disk pixels for slideshow paint. Never use m_currentSessionId —
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
    LayoutMode layoutMode() const { return m_layoutMode; }
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
    int masonryColumns() const { return m_masonryColumns; }
    /** Grid / GridCrop columns; 0 = automatic. */
    void setGridColumns(int columns);
    int gridColumns() const { return m_gridColumns; }

    /** Number of rows for LayoutMode::MasonryRows (images scale to fit row height). */
    void setMasonryRows(int rows);
    int masonryRows() const { return m_masonryRows; }

    WorkspaceItemState captureState(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);
    /** Persist session state and refresh filmstrip (chrome / toolbar edits). */
    void commitItemSessionEdit(ImageItem *item);
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
    /** Bake ±90° content into pixels and session state (not placement). */
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
    /** Apply contentQuarterTurns / contentHFlip / contentVFlip after decode+crop. */
    void applyContentBakes(ImageItem *item, const WorkspaceItemState &state);

    /**
     * Single gate for attaching *raw* decode pixels to a session image.
     *
     * FullSource: setSourceImage + SessionAppearance::applyContentToItem.
     * SoftPreview: bake appearance into a soft QImage via applyContentToImage,
     * then setPreviewImage; does not write soft dimensions into layout geometry.
     * When SoftPreview content orientation swaps aspect (odd quarter-turns),
     * Image mode refits using the oriented soft size so rapid next/prev does
     * not letterbox a rotated thumb inside the unrotated native box.
     *
     * @p sid selects appearance from m_appearance; invalid → no content bake.
     * Already-baked pixels (peer copy, undo after-image) must not use this.
     */
    void installDisplayPixels(ImageItem *item, const QImage &pixels,
                              SessionAppearance::PixelKind kind,
                              SessionImageId sid);

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
    /** User-facing quality of pixels currently shown for @p item. */
    QString pixelQualityLabel(const ImageItem *item) const;
    /** Session badge for the top-right HUD, e.g. "[3/12]", or empty. */
    QString sessionBadgeText() const;
    /** Path of the last failed Image-mode decode (empty if none). */
    QString lastLoadError() const { return m_lastLoadError; }
    /** Basename of the current/target image for the bottom HUD. */
    QString hudFileName() const;
    ImageMouseInfo mouseInfo() const { return m_mouseInfo; }
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
    /** Install host soft / schedule SoftOnly for the visible Gallery window. */
    void updateGalleryDecodeWindow();
    /** Coalesce decode-window rescans (setInterest + schedule) off the hot path. */
    void scheduleGalleryDecodeWindowRefresh(int delayMs = 48);

signals:
    void statusChanged();
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
    void sessionCropApplied(SessionImageId sessionId, const QString &path, const QImage &image);
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

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void paintViewportOverlays(QPainter &painter);
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
    /** Interactive / gallery / static flags for the current ViewMode. */
    void scheduleImageLoad(const QString &path, LoadRole role);
    /**
     * Image mode: drop the previous frame and show a loading/provisional tile
     * for @p path immediately (do not wait for the background decode).
     */
    void installImageModePendingTile(const QString &path, const QImage &preview = QImage());
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
     * Cache-only pass before Gallery pack: fill m_imageSizeByPath from
     * ThumtooCache::cachedSize and m_previewByPath from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);
    bool isProvisionalImageSize(const QString &path) const;
    /** Remember native size after a successful full decode (or async probe). */
    void rememberImageSize(const QString &path, const QSize &size);
    void scheduleImageSizeProbe(const QString &path);
    void applyProbedImageSize(const QString &path, const QSize &size);
    void scheduleGalleryDecode(const QString &path);
    /** Recover stalled soft installs (cache hit not painted / inflight stuck). */
    void gallerySoftWatchdogTick();
    /** Ladder step for item cell size in device pixels. */
    int galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes = false) const;
    ImageItem *primaryItem() const;
    QList<ImageItem *> transformTargets() const;
    /** Apply session crop from @p state to a freshly decoded item (no-op if none). */
    /**
     * Apply stored session appearance (crop + content bakes) for @p item's
     * session id / index / unbound path. Pixels must be the full on-disk image.
     */
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
    /**
     * Map item-local draft rect to source pixel rect of the *current* pixmap,
     * then compose into original on-disk coordinates in @p state.
     */
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);

    /** Crop-mode interaction (viewport chrome; item-local draft rect). */
    enum class CropHandle {
        None,
        /** Translate the draft rect without changing size. */
        Move,
        /** Rotate the draft about its centre. */
        Rotate,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        /**
         * Toggle: when on, the draft may extend outside the image (pad on apply).
         * When off, the draft is clamped to the image bounds.
         */
        ExpandToggle,
        /**
         * Shrink the draft to content: median border colour as background,
         * trim empty margins (GIMP-style autocrop). Axis-aligned.
         */
        Auto,
        /** Clears the draft rect to the full image (reset session crop on apply). */
        Reset,
        /** Leave crop mode and discard the draft. */
        Cancel,
        /** Leave crop mode and commit the draft (same as Enter). */
        Close
    };
    ImageItem *cropTargetItem() const;
    void ensureCropRectValid();
    QRectF cropRectItemLocal() const { return m_cropRect; }
    qreal cropRotation() const { return m_cropRotation; }
    /** Crop corners in item-local space (rotation about rect centre). */
    QPolygonF cropPolygonItemLocal() const;
    QRectF cropRectView() const;
    /** Viewport rects of Reset / Apply controls above the crop frame. */
    QRect cropExpandButtonView() const;
    QRect cropAutoButtonView() const;
    QRect cropResetButtonView() const;
    QRect cropCancelButtonView() const;
    QRect cropCloseButtonView() const;
    bool cropAllowExpand() const { return m_cropAllowExpand; }
    CropHandle cropHandleAt(const QPoint &viewPos) const;
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
    void attentionCommitSelectionMove();
    void attentionDeleteSelected();
    QPointF attentionViewPos(ImageItem *item, const QPointF &norm) const;
    void beginCropHandleDrag(CropHandle h, const QPoint &viewPos);
    void updateCropHandleDrag(const QPoint &viewPos);
    void endCropHandleDrag();
    void beginCropRubberBand(const QPoint &viewPos);
    void updateCropRubberBand(const QPoint &viewPos);
    void endCropRubberBand();
    void leaveCropModeInternal(bool apply);
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
    /** Workspace: shift item so crop-frame centre maps to @p sceneAnchor. */
    void alignCropFrameCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    /** Workspace: shift item so local origin (image centre) maps to @p sceneAnchor. */
    void alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    /** Cancel path: put the session crop (if any) back on the live item. */
    void restoreSessionCropAppearance(ImageItem *item);
    void updateMouseInfo(const QPoint &viewPos);
    /** Frame @p item in the view. Image mode: does not clear rotation/flips. */
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
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
    QHash<QString, WorkspaceItemState> m_itemStates;
    /**
     * Per-session-slot appearance (crop / content flip / quarter turns).
     * Keyed by session index so path duplicates stay independent value copies.
     */
    /** Per-session-image appearance, keyed by stable SessionImageId. */
    /** Content appearance by stable session-image id (Phase 2 store). */
    SessionAppearanceStore m_appearance;
    /** Gallery tiles kept while in Image mode (decoded pixels retained). */
    /** Last setWorkspacePaths order — used to keep m_items sorted for Gallery pack. */
    /**
     * Path → native pixel size (from probe or full decode). Dimensions are a
     * property of the file contents; safe to key by path (not SessionImageId).
     */
    QHash<QString, QSize> m_imageSizeByPath;
    /** Paths whose layout size is still a stand-in (probe/full decode pending). */
    QSet<QString> m_provisionalSizePaths;
    /** Paths with an in-flight async size probe. */
    QSet<QString> m_sizeProbeScheduled;
    /** Path → last provisional thumbnail (session cache for rapid next/prev). */
    QHash<QString, QImage> m_previewByPath;
    QStringList m_pathOrder;
    /** Parallel to m_pathOrder when known — SessionImageId per row (IDENTITY). */
    QVector<SessionImageId> m_sessionIdOrder;

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;

    int pageGuideHandleAt(const QPoint &viewPos) const;
    void paintPageGuideHandles(QPainter *painter) const;
    bool beginPageGuideResize(int handle);
    void updatePageGuideResize(const QPointF &scenePos, Qt::KeyboardModifiers mods);
    void endPageGuideResize();

    bool m_pageGuideVisible = false;
    QSizeF m_pageGuideSize; // scene px from printer page; empty → A4 at kPageGuideDpi
    /** When valid, overrides centred-at-origin placement (fit-to-content). */
    QRectF m_pageGuideRect;
    bool m_pageGuideSelected = false;
    int m_pageGuideHoverHandle = -1;
    int m_pageGuideDragHandle = -1;
    QRectF m_pageGuideDragStartRect;
    bool m_fitMode = true;
    bool m_fillMode = false;
    ViewMode m_viewMode = ViewMode::Image;
    bool m_imageModeNavEnabled = false;
    bool m_galleryReturnAvailable = false;
    /** Gallery: path under cursor for HUD filename (empty when none). */
    /** Last mouse position in viewport coords (gallery hover + scroll). */
    QPoint m_lastHoverViewPos;
    bool m_imageModeLeftDragPan = true;
    QColor m_bgColor{42, 42, 42};
    QColor m_bgColorAlt{48, 48, 48};
    BackgroundPattern m_bgPattern = BackgroundPattern::Checkerboard;
    bool m_bgCheckerWorkspaceOnly = true;
    WorkspaceBackground m_workspaceBackground;
    /** When true, paint AppDefault even if m_workspaceBackground is custom. */
    bool m_workspaceBackgroundShowDefault = false;
    QPixmap m_workspaceBgTile; /**< Cached tile for ImageTile mode */
    QString m_workspaceBgTilePath; /**< Path loaded into m_workspaceBgTile */
    bool m_showTextRegions = false;
    ThumtooCache::PageTextLayer m_textLayer;
    QString m_textLayerPath;
    QString m_textSearchQuery;
    bool m_textSearchFuzzy = true;
    /** Indices into m_textLayer.regions that match the current query. */
    QVector<int> m_textSearchMatches;
    bool m_textRubberbanding = false;
    QPoint m_textRubberOrigin;  /**< viewport */
    QRect m_textRubberRect;     /**< viewport, normalized while dragging */
    QVector<int> m_textSelectedRegions;
    QString m_linkHoverTip;
    bool m_hudVisible = false;
    int m_hudFontPointSize = 11;
    QColor m_hudTextColor{255, 255, 255};
    QColor m_hudPanelColor{0, 0, 0, 160};
    int m_sessionIndex = -1;
    int m_sessionTotal = 0;
    SessionImageId m_currentSessionId = kInvalidSessionImageId;
    QString m_lastLoadError;
    bool m_hudFlashVisible = false;
    /** Persistent slideshow-paused cue (top-left); not cleared by flash timer. */
    QElapsedTimer m_lastSlideshowCenterClick;
    bool m_slideshowPausedHud = false;
    /** mpv-style bottom seekbar while cursor is near the bottom edge. */
    bool m_slideshowSeekbarVisible = false;
    bool m_slideshowSeekDragging = false;
    /** Filename + index shown briefly after navigation / flash (not only when pinned). */
    bool m_hudIdentityPulse = false;
    QString m_hudAction;
    QString m_hudDetail;
    QTimer *m_hudFlashTimer = nullptr;
    /** Slideshow progress (pinned HUD only): active dwell countdown. */
    bool m_slideshowProgressActive = false;
    bool m_slideshowProgressClockPaused = false;
    qint64 m_slideshowProgressBaseMs = 0;
    /** Last [slideshow-paint] fingerprint (size/mode); skip duplicate logs. */
    QString m_lastSlideshowPaintFp;
    int m_slideshowProgressIntervalMs = 0;
    QElapsedTimer m_slideshowProgressElapsed;
    QTimer *m_slideshowProgressTimer = nullptr;
    /** Overall timeline for extended HUD (video-player style). total<=0 = off. */
    qint64 m_slideshowTimelineElapsedMs = 0;
    qint64 m_slideshowTimelineTotalMs = 0;
    SlideshowTransition m_slideshowTransition = SlideshowTransition::Crossfade;
    int m_slideshowTransitionDurationMs = 400;
    QImage m_dwellSourceImage;
    /** Pure-phase composite (driven every clock tick; no begin/cancel). */
    QString m_ssFromPath;
    QString m_ssToPath;
    QImage m_ssFromImage;
    QImage m_ssToImage;
    qreal m_ssFadeT = -1.0; // <0 = dwell
    qreal m_ssFromMotionT = 0.0;
    qreal m_ssToMotionT = 0.0;
    QPointF m_ssToBiasA{-1.0, -1.0};
    QPointF m_ssToBiasB{1.0, 1.0};
    QElapsedTimer m_ssFromMotionClock;
    QElapsedTimer m_ssToMotionClock;
    bool m_ssFromMotionClockRunning = false;
    bool m_ssToMotionClockRunning = false;
    qint64 m_ssFromMotionBaseMs = 0;
    qint64 m_ssToMotionBaseMs = 0;
    /**
     * Best unoriented raster per path for the slideshow pure-phase path.
     * Soft and sharper levels share one map; put only upgrades long-edge.
     * Camera is aspect-based, so resolution climb does not change geometry.
     */
    QHash<QString, QImage> m_ssRasterByPath;
    QPixmap m_dwellAtlas; /**< Pre-scaled for dwell; rebuilt on source/resize */
    qreal m_dwellAtlasScale = 0.0;
    int m_dwellAtlasVw = 0;
    int m_dwellAtlasVh = 0;
    qreal m_dwellMotionT = 0.0; /**< Latest dwell progress [0,1] */
    SlideshowMotion m_slideshowMotion = SlideshowMotion::Off;
    qreal m_panZoomFactor = 1.12; /**< PanZoom end/start scale */
    SlideshowZoom m_slideshowZoom = SlideshowZoom::Fit;
    SlideshowLetterboxFill m_slideshowLetterboxFill = SlideshowLetterboxFill::AppBackground;
    QColor m_slideshowPadColor{42, 42, 42};
    /** Rapid keyboard nav — skip ZoomBlur work until settled. */
    bool m_slideshowNavHot = false;
    /** Two-slot ZoomBlur underlay cache (from/to during transitions). */
    mutable QPixmap m_zoomBlurUnderlay[2];
    mutable qint64 m_zoomBlurSourceKey[2] = {0, 0};
    mutable int m_zoomBlurVw = 0;
    mutable int m_zoomBlurVh = 0;
    /**
     * Last successfully built underlay — drawn while a new blur is in flight so
     * rapid flips never flash solid pad or block the GUI on CPU blur.
     */
    mutable QPixmap m_zoomBlurLastGood;
    mutable qint64 m_zoomBlurLastGoodKey = 0;
    /** Bumped on every slide change; stale async blur jobs no-op on completion. */
    mutable quint64 m_zoomBlurGeneration = 0;
    /// Up to two concurrent blur builds (from+to underlays in a transition).
    mutable quint64 m_zoomBlurInFlightGen[2] = {0, 0};
    mutable qint64 m_zoomBlurInFlightKey[2] = {0, 0};
    bool m_slideshowMotionActive = false;
    bool m_slideshowMotionPaused = false;
    /** Scroll policies restored when Ken Burns underlay returns. */
    Qt::ScrollBarPolicy m_motionSavedHBarPolicy = Qt::ScrollBarAsNeeded;
    Qt::ScrollBarPolicy m_motionSavedVBarPolicy = Qt::ScrollBarAsNeeded;
    bool m_motionSavedBarPolicies = false;

    /** Corner/edge biases for Ken Burns paths (dwell + dual-blit). */
    QPointF m_motionBiasA{-1.0, -1.0};
    QPointF m_motionBiasB{1.0, 1.0};
    bool m_motionBiasValid = false;
    /** Path for which m_motionBiasA/B were chosen; invalidated on manual next/prev. */
    QString m_motionBiasPath;
    /** Last scene-space travel direction (end - start), for continuing legs. */
    QPointF m_motionTravelDir{0.0, 1.0};
    /** Fixed session pan sign (+1 / -1); never reverses mid-show. */
    qreal m_motionSign = 1.0;
    int m_motionDurationMs = 0;
    /** Added to m_motionClock.elapsed() for motion path continuity. */
    qint64 m_motionElapsedOffsetMs = 0;
    QElapsedTimer m_motionClock;
    QTimer *m_motionTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutMode m_layoutMode = LayoutMode::FreeForm;
    int m_masonryColumns = 3;
    int m_gridColumns = 0;
    int m_masonryRows = 3;
    std::atomic<quint64> m_loadGeneration{0};
    /** Outstanding LoadAdd / gallery decode jobs per path (refcount). */
    QHash<QString, int> m_pendingWorkspacePaths;
    /**
     * Per-path Gallery soft-thumb state (see updateGalleryDecodeWindow).
     *
     * have      — long edge of soft pixels on the item (0 = none)
     * want      — last computed target ladder step from visibility + zoom
     * inflight  — edge currently requested (0 = idle); at most one per path
     * gaveUpWant— highest want we finished without ~90% delivery; do not retry
     *             the same want (stops schedulePixels/ladderReady storms)
     * failed    — permanent hard failure
     */
    struct GallerySoftState {
        int have = 0;
        int want = 0;
        int inflight = 0;       // soft ladder edge in flight
        bool fullInflight = false; // ImageLoader::load (native) in flight
        int gaveUpWant = 0;
        bool failed = false;
        qint64 inflightSinceMs = 0; // QDateTime::currentMSecsSinceEpoch when inflight set
    };
    QHash<QString, GallerySoftState> m_gallerySoft;

    int gallerySoftInflightCount() const;
    void gallerySoftResetPath(const QString &path);
    void gallerySoftResetAll();
    int galleryWantEdgeForPath(const QString &path, const QRectF &sceneVisible) const;
    /** @deprecated Gallery always virtualizes; kept for ABI/docs only. */
    static constexpr int kGalleryVirtualThreshold = 1;
    static constexpr int kGalleryDecodeOverscanPx = 400;
    /** Default Gallery soft/display worker slots; override BILTOO_GALLERY_DECODE_CONCURRENCY. */
    static constexpr int kMaxConcurrentGalleryDecodes = 4;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    static int galleryDecodeConcurrency();
    /** Queue of workspace restores still waiting for decode (supports same path twice). */
    QList<WorkspaceItemState> m_pendingRestoreStates;
    /** Optional scene centre for in-flight LoadAdd decodes (e.g. drops). */
    QHash<QString, QPointF> m_pendingScenePos;
    /** Session slot to assign when a LoadAdd for @p path finishes. */
    QHash<QString, int> m_pendingSessionIndexByPath;
    struct PendingSessionBind {
        QString path;
        SessionImageId id = kInvalidSessionImageId;
        int index = -1;
        QPointF scenePos;
        bool hasScenePos = false;
    };
    QList<PendingSessionBind> m_pendingSessionBinds;
    /** Select these session ids when LoadAdd creates their tiles (paste). */
    QSet<SessionImageId> m_pendingSelectSessionIds;
    /** Content appearance staged by Duplicate until bindSelectedSessionIds. */
    QHash<ImageItem *, WorkspaceItemState> m_pendingItemAppearance;
    ImageMouseInfo m_mouseInfo;

    QPoint m_lastMousePos;
    bool m_panning = false;
    /** One-shot rubber-band zoom (Z): armed until drag completes or Esc. */
    bool m_zoomRegionArmed = false;
    bool m_zoomRegionDragging = false;
    QPoint m_zoomRegionOrigin;
    class QRubberBand *m_zoomRubberBand = nullptr;

    bool m_rotating = false;
    ImageItem *m_rotateItem = nullptr;
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateItemStart = 0.0;

    ImageItem *m_handleDragItem = nullptr;

    bool m_cropMode = false;
    bool m_attentionMode = false;
    bool m_attentionDragging = false;
    bool m_attentionRubberbanding = false;
    QPoint m_attentionRubberOrigin;
    QRect m_attentionRubberRect; // viewport coords
    QVector<int> m_attentionSelected; // indices into attentionPoints
    QVector<QPointF> m_attentionDragStartPts; // snapshot at press for selected move
    QPoint m_attentionDragOriginView;
    bool m_attentionDraftValid = false;
    QVector<QPointF> m_attentionDraftPts;
    SessionImageId m_attentionDraftSessionId = kInvalidSessionImageId;
    /** Locked crop subject for the whole crop session (IDENTITY.md). */
    SessionImageId m_cropTargetId = kInvalidSessionImageId;
    /** Non-owning; ImageItem is not a QObject so QPointer is unavailable. */
    ImageItem *m_cropTargetItem = nullptr;
    /** Draft may extend outside the image; apply pads with background. */
    bool m_cropAllowExpand = false;
    /** Draft crop rotation (degrees, about m_cropRect centre). */
    qreal m_cropRotation = 0.0;
    qreal m_cropRotateStartAngle = 0.0;
    qreal m_cropRotateStartRotation = 0.0;
    /** True while crop mode shows the full on-disk image (not the cropped pixmap). */
    bool m_cropShowingFullImage = false;
    /** Workspace free-rotate stashed while crop runs axis-aligned. */
    qreal m_cropStashedPlacementRotation = 0.0;
    qreal m_cropStashedPlacementShear = 0.0;
    bool m_cropHadStashedPlacement = false;
    /** Appearance + session state when crop mode was entered (for Apply undo). */
    QImage m_cropEnterSource;
    WorkspaceItemState m_cropEnterState;
    bool m_cropEnterValid = false;
    /** Draft crop in crop-target item local coordinates (contentRect space). */
    QRectF m_cropRect;
    CropHandle m_cropActiveHandle = CropHandle::None;
    CropHandle m_cropHoverHandle = CropHandle::None;
    bool m_cropRubberBanding = false;
    QPointF m_cropRubberOriginLocal;
    QRectF m_cropDragStartRect;
    QPointF m_cropDragStartLocal;

    /** Multi-select: which group handle is active (-1 = none). 0–7 scale, 8–11 rotate. */
    int m_groupHandle = -1;
    int m_groupHoverHandle = -1;
    bool m_groupScaleDrag = false;
    bool m_groupRotateDrag = false;
    QRectF m_groupBoundsStart;
    QPointF m_groupCenterStart;
    QPointF m_groupPressScenePos;
    qreal m_groupPressAngleDeg = 0.0;
    QList<WorkspaceItemState> m_groupDragStartStates;
    QList<ImageItem *> m_groupDragItems;
    ImageItem *m_dragItem = nullptr;
    WorkspaceItemState m_dragStartState;

    bool m_applyingLayout = false;
    /** Nested suppress: Gallery delete must not repack via resizeEvent. */
    int m_galleryRelayoutSuppressCount = 0;
    QTimer *m_galleryDecodeScrollTimer = nullptr;
    QTimer *m_statusRefreshTimer = nullptr;
    /** BILTOO_PERF / THUMTOO_DEBUG: paint + decode-window timings. */
    bool m_perfEnabled = false;
    QElapsedTimer m_perfFpsClock;
    int m_perfFrameCount = 0;
    qreal m_perfFps = 0.0;
    qint64 m_perfLastPaintUs = 0;
    qint64 m_perfLastDecodeWindowUs = 0;
    qint64 m_perfMaxDecodeWindowUs = 0;
    int m_perfDecodeWindowRuns = 0;

    QTimer *m_gallerySoftWatchdog = nullptr;
    QTimer *m_layoutDebounceTimer = nullptr;
    GalleryPackReason m_debouncedPackReason = GalleryPackReason::ContentChange;
};

#endif // IMAGEVIEW_H
