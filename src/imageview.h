// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H
#include "imageview_types.h"
#include "gallery/gallerysizeresolve.h"
#include "display/tileneighborprefetch.h"
#include "crop/cropsession.h"
#include "crop/cropgeometry.h"
#include "crop/cropflash.h"
#include "attention/attentionsession.h"
#include "shell/centreprogress.h"
#include "workspace/grouptransformsession.h"
#include "workspace/pageguidesession.h"
#include "item/iteminteractsession.h"
#include "hud/hudflash.h"
#include "view/viewframing.h"
#include "text/textlayersession.h"
#include "slideshow/zoomregiongesture.h"
#include "view/canvasbackground.h"
#include "gallery/layoutprefs.h"
#include "view/viewportchrome.h"
#include "hud/hudappearance.h"
#include "session/sessionchrome.h"
#include "gallery/gallerydecodebook.h"
#include "util/perfstats.h"
#include "color/coloradjustcommit.h"
#include "gallery/layoutdebounce.h"
#include "gallery/galleryrelayoutsuppress.h"
#include "gallery/layoutapplyguard.h"
#include "item/imagesizebook.h"
#include "item/pathitemstatebook.h"
#include "item/pendingitemappearancebook.h"
#include "slideshow/slideshowtypes.h"
#include "slideshow/motionscrollchrome.h"
#include "display/loadgeneration.h"
#include "session/sessionloadgate.h"
#include "session/sessionbindbook.h"
#include "session/sessionpathorder.h"
#include "session/packorderview.h"
#include "session/packorderoverlay.h"
#include "host/thumtoocache.h"
#include "color/coloradjust.h"
#include "session/sessionappearance.h"
#include "session/sessionseedbook.h"
#include "item/itemworld.h"
#include "session/sessiondocument.h"
#include "gallery/gallerycontroller.h"
#include "slideshow/slideshowcontroller.h"
#include "crop/cropcontroller.h"
#include "attention/attentioncontroller.h"
#include "display/displaypipelinecontroller.h"
#include "workspace/workspacecontroller.h"
#include "image/imagecontroller.h"
#include "display/pathrasterservice.h"
#include "display/tile_load_coordinator.h"
#include "display/displaysurface.h"
#include "gallery/gallerylayout.h"
#include <QColor>
#include <QPixmap>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QHash>
#include <memory>
#include <vector>

namespace tilelod { class TileLodController; }
#include <functional>
#include <utility>
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
     * Declared early so transition APIs (enterGallery)
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
    void setImageModeSoftProvider(ImageModeSoftProvider provider)
    {
        m_imageModeSoftProvider = std::move(provider);
    }

    ~ImageView() override;
    /**
     * Add (or select) the canvas instance bound to @p sessionId / @p sessionIndex.
     * SessionImageId is required — path alone is not identity (IDENTITY.md).
     */
    bool addImageForSession(const QString &path, SessionImageId sessionId, int sessionIndex);
    /**
     * Workspace: place @p path at @p scenePos for @p sessionId. If that session
     * image is already on the canvas, move it; otherwise create a bound tile.
     */
    bool placeOrMoveImageAt(const QString &path, const QPointF &scenePos,
                            SessionImageId sessionId, int sessionIndex);
    /**
     * Session list order for @p item. Prefers SessionDocument::indexOfId when
     * the item is bound; falls back to the cached ImageItem::sessionIndex().
     * List position is not identity (IDENTITY.md) — use sessionId for that.
     */
    int sessionListIndex(const ImageItem *item) const;
    /**
     * True when @p id is invalid, unbound in the document, or the document row
     * for @p id has path @p path. False when id is bound to a different path.
     */
    bool sessionIdMatchesPath(SessionImageId id, const QString &path) const;
    /**
     * Stamp list-order cache from document when bound; clear to -1 when unbound.
     * Pack-row hints must be restamped by the caller after refresh.
     * Stage 2 residual: call after setSessionId so the cache cannot lag
     * document order. Returns the stamped index, or -1 when unbound/unknown.
     */
    int refreshSessionIndexCache(ImageItem *item);
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
    /**
     * Progress HUD title/detail (archive expand, size resolve, tile load, …).
     * Blocking messages paint centred; non-blocking (size resolve, tiles) top-left.
     * Suppresses the empty-session invite while set. Cleared with clearCentreProgress().
     */
    void setCentreProgress(const QString &title, const QString &detail = QString());
    void clearCentreProgress();
    /** Controller host: set m_viewMode + m_layout.currentMode() and refresh viewport. */
    void setActiveMode(ViewMode mode, LayoutMode layout);
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
    /**
     * Logical image size for @a path (never soft-raster dimensions).
     * Lookup only: m_sizeBook, then thumtoo cache. Empty if unknown.
     * Slideshow and Image-mode framing share this.
     */
    QSize logicalSizeForPath(const QString &path) const override;
    /**
     * Content layout size for a session row (or unbound path): native file size
     * × ItemWorld content ops (SessionImageId). Same rule as filmstrip provider
     * and applyContentLayoutSize — never soft sample dims, never native alone
     * when orient/crop is known.
     */
    QSize contentLayoutSize(const QString &path, SessionImageId sessionId) const;
    // Host accessors (controllers): path raster, books, prefs — imageview_host_accessors.inc
#include "imageview_host_accessors.inc"

    /**
     * Phase 7 Stage 0 facade (appearance / path-book / size-book).
     * Pack-order host mutators live in imageview_host_pipeline.inc
     * (pathOrderClear / SetOrder / currentPackOrder; AppendRow is private).
     */
    ItemWorld &itemWorld() { return m_itemWorld; }
    const ItemWorld &itemWorld() const { return m_itemWorld; }
    /**
     * Bind SessionDocument seed book (required before hostSeedBook()).
     * Content appearance is ItemWorld sparse tables — not this store.
     */
    void bindSessionSeedBook(SessionSeedBook *store)
    {
        m_seedBook = store;
    }
    /**
     * Bind working SessionDocument. firstSessionIdForPath prefers the document;
     * LoadAdd multiplicity stays on the pack-order overlay.
     */
    void bindSessionDocument(SessionDocument *doc)
    {
        m_sessionDoc = doc;
    }
    SessionDocument *sessionDocument() const { return m_sessionDoc; }
    void takePendingWorkspacePath(const QString &path);


// Crop/display ops — imageview_host_crop_display.inc
#include "imageview_host_crop_display.inc"
// Mode/canvas ops (find/destroy/gallery/page-guide) — imageview_host_ops.inc
#include "imageview_host_ops.inc"

    void setViewMode(ViewMode mode);


    /** Reset view/scene so Image mode is not affected by prior canvas state. */
    void prepareImageModeCanvas();
    bool isImageMode() const { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const { return m_viewMode == ViewMode::Workspace; }


    /**
     * Enable left/right edge navigation affordances in Image mode.
     * Typically true when the session has more than one image.
     */
    void setImageModeNavigationEnabled(bool on);
    /** When true, Image mode top edge offers return-to-gallery. */
    void setGalleryReturnAvailable(bool on);

    /**
     * Show exactly the given session rows on the workspace. @p sessionIds must
     * align with @p paths (one id per path). Images already present keep their
     * live transform; newly added ones restore saved state or get a default
     * placement. Images no longer listed are removed after their state is saved.
     * Path-only rebuild is removed — callers pass session ids so duplicate
     * paths stay distinct (IDENTITY.md).
     */
    void setWorkspacePaths(const QStringList &paths,
                           const QVector<SessionImageId> &sessionIds);
    /**
     * Reorder canvas items to match session/pack order. When @p ids align with
     * @p paths, prefer findItemBySessionId so duplicate paths stay distinct
     * (IDENTITY). Path first-unseen is fallback for unbound rows only.
     */
    void reorderItemsByPaths(const QStringList &paths,
                             const QVector<SessionImageId> &ids = {});


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
    /**
     * Gallery: scroll @p sessionId into view without clearing multi-select
     * (preferred over path when the tile is bound).
     */
    void revealGallerySessionId(SessionImageId sessionId);
    void removeWorkspaceSessionId(SessionImageId sessionId);
    /** How many canvas items currently show @p path. */
    int workspacePathOccurrenceCount(const QString &path) const;
    void setTool(Tool tool);


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
    /** Restore pixels + session crop metadata (used by crop undo/redo). */
    void applyCropAppearance(ImageItem *item, const QImage &src,
                            const WorkspaceItemState &state);


    /** Debug: paint text/link region rects for page documents (Image mode). */
    void setShowTextRegions(bool on);

    /**
     * Highlight regions whose text contains @p query (case-insensitive).
     * Empty query clears highlights. Loads the text layer if needed.
     * Returns number of matching regions on the current page.
     */
    int setTextSearchQuery(const QString &query);
    bool hasTextLayer() const;
    int textLayerRegionCount() const;
    /** Soft match for OCR noise (alnum-only + light edit distance). Default on. */
    void setTextSearchFuzzy(bool on);
    /** True if @p regionText matches @p query under the same rules as Find. */
    static bool textMatchesQuery(const QString &regionText, const QString &query, bool fuzzy);

    /** Copy selected text to the clipboard; returns false if nothing selected. */
    bool copySelectedText();

    void setBackgroundColor(const QColor &color);
    /**
     * Effective solid pad colour for slideshow letterbox (Solid mode colour,
     * else Preferences background). Used when ZoomBlur cannot run.
     */
    QColor slideshowPadColor() const;
    void setBackgroundColorAlt(const QColor &color);
    void setBackgroundPattern(BackgroundPattern pattern);
    /** When true, checkerboard is used only in Workspace; other modes stay solid. */
    void setCheckerboardWorkspaceOnly(bool on);

    /**
     * Per-Workspace background override (project state). AppDefault uses the
     * preference colours/pattern (technical default) instead of a custom look.
     */
    void setWorkspaceBackground(const WorkspaceBackground &bg);
    void clearWorkspaceBackground(); /**< AppDefault */
    /**
     * Temporary view of the Preferences / technical background without changing
     * the project WorkspaceBackground override. Used by the Background Default
     * toolbar toggle.
     */
    void setWorkspaceBackgroundShowDefault(bool on);

    /**
     * Session Gallery / Image canvas override (not Preferences, not project).
     * AppDefault follows Preferences materials. Shared across Gallery and Image.
     */
    void setViewBackground(const WorkspaceBackground &bg);

    /**
     * Session position for status line and HUD (index/total, 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     * @p pulseIdentity briefly shows filename/badge without a pinned HUD (H);
     * use false for silent updates (e.g. slideshow auto-advance).
     */
    void setCurrentSessionId(SessionImageId id);
    /**
     * Exclusive-select @p item; ensure visible + HUD hover in Gallery.
     * Prefer this when the live ImageItem is already known (keyboard nav).
     */
    void focusGalleryItem(ImageItem *item);
    /** Exclusive-select live item bound to @p sessionId (identity-correct). */
    void focusSessionId(SessionImageId sessionId);
    /** Select preferred/first canvas item for @p path; ensure visible in Gallery. */
    void focusSessionPath(const QString &path);

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    /** Corner marks for crop / orient / grade (default on). */
    void setContentEditMarksVisible(bool on);
    bool contentEditMarksVisible() const;
    void setHudFontPointSize(int pt);
    void setHudTextColor(const QColor &color);
    void setHudPanelColor(const QColor &color);

    /**
     * Brief top-left HUD action (slideshow, fit mode, …).
     * Visible for ~1.8s; never used for Next/Prev (session badge covers that).
     */
    void flashHud(const QString &action, const QString &detail = QString());

    // Slideshow dwell/timeline/phase/Ken Burns/pause cues: SlideshowController
    // (hostSlideshow()). Not re-exported on ImageView.

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
    /**
     * Duplicate selected tiles. @p newIds are pre-allocated session images
     * (one per source, same order as selection walk). Each copy is bound
     * immediately — no unbound window before MainWindow membership update.
     * @p firstSessionIndex stamps list-order cache on copies when >= 0.
     */
    void duplicateSelected(const QVector<SessionImageId> &newIds,
                           int firstSessionIndex = -1);

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
    /** Enter Gallery mode and apply the given packaged layout (not FreeForm). */
    void enterGallery(LayoutMode packagedLayout);


    /**
     * Interaction snapshot of a live tile: sparse-prefer store content
     * (sessionAppearanceValue) plus live pose / applied ContentXform / grade.
     * For durable reads prefer sessionAppearanceValue; use captureState when
     * mid-edit live authority must win (undo, crop draft).
     */
    WorkspaceItemState captureState(const ImageItem *item) const;
    /**
     * Freeze path for mode leave / persist / remove undo: store + live pose/grade
     * when durable appearance exists and applied ContentXform is absent; otherwise
     * captureState (mid-edit or unbound). Prefer this over ad-hoc store+live.
     */
    WorkspaceItemState freezeItemAppearance(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);
    /**
     * Install applied ContentXform fingerprint on the live ImageItem.
     * ItemWorld remains authority for bound ids. Applied survives pixel clear.
     */
    void syncLiveContentMetaFromState(ImageItem *item, const WorkspaceItemState &state);
    /**
     * Install live color grade on the item (record-only or rebuild display).
     * ItemWorld Color table remains authority for bound ids.
     */
    void syncLiveColorFromState(ImageItem *item, const ColorAdjustments &grade,
                                bool rebuildDisplay = false);
    /**
     * Identity clear: drop applied ContentXform fingerprint.
     */
    void clearLiveContentMeta(ImageItem *item);
    /**
     * Mode-stash restore: if this tile still carries an applied fingerprint that
     * no longer matches ItemWorld want, drop it so rematerialize uses the store
     * only (ECS_GUI_BYPASSES #6). No-op when unbound, no durable row, or match.
     */
    void clearStaleAppliedFingerprintIfNeeded(ImageItem *item);
    /**
     * Applied ContentXform fingerprint for @p item.
     * Prefer ItemWorld runtime table when bound (Stage 2 residual 2081);
     * fall back to ImageItem mirror (paint / unbound).
     */
    bool itemHasAppliedContentXform(const ImageItem *item) const;
    ContentXform::Value itemAppliedContentXform(const ImageItem *item) const;
    /**
     * Live colour grade for @p item (slider / paint lag).
     * Prefer ItemWorld runtime lag when bound (host-side scratch); ImageItem
     * mirror for paint / unbound. Durable grade is ItemWorld Color.
     */
    ColorAdjustments itemLiveColor(const ImageItem *item) const;
    /**
     * Clear display pixels on @p item (keeps applied ContentXform fingerprint).
     * Sole external clear path — ImageItem::clearDecodedPixels is private.
     */
    void clearItemDecodedPixels(ImageItem *item);
    /** Host path for logical layout size (ImageItem::setIntrinsicSize is private). */
    void setItemIntrinsicSize(ImageItem *item, const QSize &size);
    void setItemSessionId(ImageItem *item, SessionImageId id);
    void setItemSessionIndex(ImageItem *item, int index);
    /**
     * Soft stand-in when install left no display pixels (Gallery soft path).
     * Sole external setPreviewImage path.
     */
    void setItemPreviewImage(ImageItem *item, const QImage &preview);
    /** Persist session state and refresh filmstrip (chrome / toolbar edits). */
    void commitItemSessionEdit(ImageItem *item);
    /** Copy of stored appearance for @p id (empty/default if none). */
    /**
     * Bound appearance for @p id (ItemWorld::appearanceValue — sparse-prefer).
     */
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
    QSize imageSize() const;
    int itemCount() const;
    /** Live tiles, stashed tiles, or durable snapshot — Workspace is non-empty. */
    bool hasWorkspaceContent() const;
    QStringList itemPaths() const;
    /** Live canvas SessionImageIds (parallel to itemPaths; invalid when unbound). */
    QVector<SessionImageId> itemSessionIds() const;
    /** Paths of selected canvas items (Gallery/Workspace). Image mode: current path. */
    QStringList selectedPaths() const;
    /** Select every live canvas tile (Gallery / Workspace). No-op in Image mode. */
    void selectAllCanvasItems();
    /** In-flight LoadAdd / LoadRestore / viewport-window decodes. */
    int pendingDecodeCount() const;
signals:
    void stickyZoomChanged();
    void statusChanged();
    /** Packaged Gallery: all session size probes settled (or timed out). */
    void gallerySizeResolveFinished();
    void mouseInfoChanged(const ImageMouseInfo &info);
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
    /** Crop mode toggled on/off (toolbar checkable state). */
    void cropModeChanged(bool active);
    void attentionModeChanged(bool active);
    /**
     * Session crop committed for filmstrip (id-keyed; never path-only).
     * @p image is the new displayed pixels; @p hasCrop marks sticky crop bake.
     */
    void sessionCropApplied(SessionImageId sessionId, const QString &path, const QImage &image,
                           bool hasCrop);
    /**
     * Flip / rotate / grade appearance for filmstrip (id-keyed; never path-only).
     * Path is informational; ThumbnailBar overrides by SessionImageId.
     */
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
    /** Gallery focus by session list index (unbound tile with list-order cache). */
    void sessionSlotFocused(int sessionIndex);
    /** Gallery focus by stable session id (preferred when tile is bound). */
    void sessionImageFocused(SessionImageId sessionId);
    /** Gallery: remove selected session images by id (duplicate-safe). */
    void sessionRemoveIdsRequested(const QVector<SessionImageId> &ids);
    /** Gallery: remove by session list indices (unbound tiles with list-order cache). */
    void sessionRemoveIndicesRequested(const QList<int> &indices);
    /** Gallery: path-only remove for unbound tiles without list index. */
    void sessionRemovePathsRequested(const QStringList &paths);
    /** File URLs dropped onto the view (same semantics as MainWindow). */
    void filesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                      const QPointF &scenePos, bool hasScenePos,
                      const QList<qint64> &sessionIds = {},
                      const QStringList &internalPaths = {});
public:
    // Pack-order + load/framing host surface (controllers).
    // Paint/input try* phases: imageview_private_methods.inc.
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
    void flushAppliedContentToItemWorld();

    /** Geometry undo command (transform_actions); needs private session-state APIs. */
    friend class ImageViewTransformGeometryCommand;

    // Phase 6 Tier 0: privatized methods — see REFACTOR.md / imageview_private_methods.inc
#include "imageview_private_methods.inc"
#include "imageview_private_rest.inc"
};
#endif // IMAGEVIEW_H
