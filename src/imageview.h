// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include "imageview_types.h"
#include "sessionappearance.h"
#include "gallerycontroller.h"
#include "workspacecontroller.h"
#include "imagecontroller.h"

#include <QColor>
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
#include <QUrl>

#include <atomic>

class ImageItem;
class QGraphicsScene;
class QTimer;
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

    friend class GalleryController;
    friend class WorkspaceController;
    friend class ImageController;

public:
    enum class Tool {
        Select,
        Pan
    };

    enum class BackgroundPattern {
        Solid,
        Checkerboard
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
        MasonryRows
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
    /**
     * Assign sessionIndex from @p sessionFiles order (legacy). Prefer
     * rebindWorkspaceSession with stable ids.
     * Matches by path occurrence so duplicates and older unbound items work.
     */
    void rebindWorkspaceSessionIndices(const QStringList &sessionFiles);
    void rebindWorkspaceSession(const QStringList &sessionFiles,
                                const QVector<SessionImageId> &sessionIds);
    void clearWorkspace();
    /** Clear drag/group/rotate interaction pointers (items stay on canvas). */
    void clearInteractionState();
    /** Controller host: stop layout debounce and clear applyingLayout. */
    void stopDeferredPacking();
    /** Controller host: set m_viewMode + m_layoutMode and refresh viewport. */
    void setActiveMode(ViewMode mode, LayoutMode layout);
    /** Controller host: return classic path and clear it. */
    QString takeClassicPath();
    /** Controller host: drop in-flight workspace/gallery pending load queues. */
    void clearPendingLoads();
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
    /** Controller host: session path order used for Gallery packing. */
    QStringList &pathOrder() { return m_pathOrder; }
    const QStringList &pathOrder() const { return m_pathOrder; }
    /** Controller host: disable Image-mode fit/fill when restoring free-form. */
    void clearFitFillModes();
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
    QList<WorkspaceItemState> &pendingRestoreStates() { return m_pendingRestoreStates; }
    const QList<WorkspaceItemState> &pendingRestoreStates() const { return m_pendingRestoreStates; }
    /** Destroy live canvas items only; keep Workspace/Gallery stashes. */
    void clearLiveCanvas();

    /** Workspace: show a paper-sized frame (scene units) for print layout. */
    void setPageGuideVisible(bool on);
    bool pageGuideVisible() const { return m_pageGuideVisible; }
    /** Update guide size from a printer page layout (millimetres → scene px). */
    void setPageGuideFromPrinter(const QPrinter &printer);
    /** Scene rectangle of the page guide (centred on the origin). */
    QRectF pageGuideSceneRect() const;
    /** Scene pixels per millimetre for the page guide (layout scale). */
    static qreal pageGuidePxPerMm();
    /** Paint current mode content into @p pageRect on @p painter (print/preview). */
    void renderForPrint(QPainter *painter, const QRectF &pageRect) const;
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
    void removeWorkspaceSessionId(SessionImageId sessionId);
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
    /** Commit the draft crop rect to pixels and leave crop mode. */
    void applyCrop();
    /** Discard the draft and leave crop mode. */
    void cancelCrop();
    /** Restore pixels + session crop metadata (used by crop undo/redo). */
    void applyCropAppearance(ImageItem *item, const QImage &src,
                            const WorkspaceItemState &state);

    /** When true (default), left-drag pans in Image mode. */
    void setImageModeLeftDragPan(bool on);
    bool imageModeLeftDragPan() const { return m_imageModeLeftDragPan; }

    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_bgColor; }
    void setBackgroundColorAlt(const QColor &color);
    QColor backgroundColorAlt() const { return m_bgColorAlt; }
    void setBackgroundPattern(BackgroundPattern pattern);
    BackgroundPattern backgroundPattern() const { return m_bgPattern; }
    /** When true, checkerboard is used only in Workspace; other modes stay solid. */
    void setCheckerboardWorkspaceOnly(bool on);
    bool checkerboardWorkspaceOnly() const { return m_bgCheckerWorkspaceOnly; }

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
    /** Workspace: clone selection (same path, independent transforms). */
    void duplicateSelected();

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }
    void applyLayout(GalleryPackReason reason = GalleryPackReason::ExplicitLayout);
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
    /** Copy of stored appearance for @p id (empty/default if none). */
    WorkspaceItemState sessionAppearanceValue(SessionImageId id) const;
    bool hasSessionAppearance(SessionImageId id) const;
    /** Restore appearance after session undo (store only; no canvas mutate). */
    void setSessionAppearance(SessionImageId id, const WorkspaceItemState &state);
    /** Bake ±90° content into pixels and session state (not placement). */
    void bakeItemRotate90(ImageItem *item, int quarterTurns);
    /** Bake flip into pixels and session state. */
    void bakeItemFlip(ImageItem *item, bool horizontal, bool vertical);
    /** Apply contentQuarterTurns / contentHFlip / contentVFlip after decode+crop. */
    void applyContentBakes(ImageItem *item, const WorkspaceItemState &state);

    QString statusText() const;
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
    QStringList itemPaths() const;
    /** Paths of selected canvas items (Gallery/Workspace). Image mode: current path. */
    QStringList selectedPaths() const;
    /** In-flight LoadAdd / LoadRestore / viewport-window decodes. */
    int pendingDecodeCount() const;

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
    void navigateNextRequested();
    /** Image mode: user activated top-edge return (Gallery or Workspace). */
    void galleryReturnRequested();
    /** Image mode: double-click requests fullscreen toggle. */
    void fullscreenToggleRequested();
    /** Crop mode toggled on/off (toolbar checkable state). */
    void cropModeChanged(bool active);
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
                      const QPointF &scenePos);

public slots:
    /** Deliver a finished background decode (generation must still match). */
    void onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                       int role);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
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
    void applyItemModeFlags(ImageItem *item);
    void scheduleImageLoad(const QString &path, LoadRole role);
    ImageItem *createPlaceholderItem(const QString &path, const QSize &intrinsicSize);
    QSize probeImageSize(const QString &path) const;
    void updateGalleryDecodeWindow();
    void scheduleGalleryDecode(const QString &path);
    ImageItem *findItemByPath(const QString &path) const;
    ImageItem *primaryItem() const;
    ImageItem *targetItem() const;
    QList<ImageItem *> transformTargets() const;
    /** Apply session crop from @p state to a freshly decoded item (no-op if none). */
    void applySessionCrop(ImageItem *item, const WorkspaceItemState &state);
    /**
     * Apply stored session appearance (crop + content bakes) for @p item's
     * session id / index / unbound path. Pixels must be the full on-disk image.
     */
    void applyStoredAppearance(ImageItem *item);
    /**
     * Map item-local draft rect to source pixel rect of the *current* pixmap,
     * then compose into original on-disk coordinates in @p state.
     */
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);

    /** Crop-mode interaction (viewport chrome; item-local draft rect). */
    enum class CropHandle {
        None,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        /** Clears the draft rect to the full image (reset session crop on apply). */
        Reset,
        /** Commits the draft crop (same as Enter). */
        Apply
    };
    ImageItem *cropTargetItem() const;
    void ensureCropRectValid();
    QRectF cropRectItemLocal() const { return m_cropRect; }
    QRectF cropRectView() const;
    /** Viewport rects of Reset / Apply controls above the crop frame. */
    QRect cropResetButtonView() const;
    QRect cropApplyButtonView() const;
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    void paintCropOverlay(QPainter &painter);
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
    void destroyCanvasItem(ImageItem *item);
    /** Expand sceneRect around free-form items so pan/scrollbars have range. */
    void updateWorkspaceSceneRect();
    void snapshotWorkspace();
    void restoreWorkspace();
    /** Detach Workspace tiles from the scene (keep decoded pixels + view). */
    void stashWorkspaceItems();
    /** Reattach stashed Workspace tiles; no-op if empty. */
    void restoreStashedWorkspaceItems();
    void discardStashedWorkspace();
    /** Detach Gallery tiles from the scene (keep decoded pixels). */
    void stashGalleryItems();
    /** Reattach stashed Gallery tiles; no-op if empty. */
    void restoreStashedGalleryItems();
    void snapshotFreeFormStates();
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
    QString m_classicPath;
    /** Last setWorkspacePaths order — used to keep m_items sorted for Gallery pack. */
    QStringList m_pathOrder;

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;

    bool m_pageGuideVisible = false;
    QSizeF m_pageGuideSize; // scene px from printer page; empty → A4 at kPageGuideDpi
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
    bool m_hudVisible = false;
    int m_hudFontPointSize = 11;
    QColor m_hudTextColor{255, 255, 255};
    QColor m_hudPanelColor{0, 0, 0, 160};
    int m_sessionIndex = -1;
    int m_sessionTotal = 0;
    SessionImageId m_currentSessionId = kInvalidSessionImageId;
    QString m_lastLoadError;
    bool m_hudFlashVisible = false;
    /** Filename + index shown briefly after navigation / flash (not only when pinned). */
    bool m_hudIdentityPulse = false;
    QString m_hudAction;
    QString m_hudDetail;
    QTimer *m_hudFlashTimer = nullptr;
    /** Slideshow progress (pinned HUD only): active dwell countdown. */
    bool m_slideshowProgressActive = false;
    int m_slideshowProgressIntervalMs = 0;
    QElapsedTimer m_slideshowProgressElapsed;
    QTimer *m_slideshowProgressTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutMode m_layoutMode = LayoutMode::FreeForm;
    int m_masonryColumns = 3;
    int m_gridColumns = 0;
    int m_masonryRows = 3;
    std::atomic<quint64> m_loadGeneration{0};
    QSet<QString> m_pendingWorkspacePaths;
    /** Gallery virtualization: paths scheduled for decode this window. */
    QSet<QString> m_galleryDecodeScheduled;
    /** Paths that failed decode — do not spin forever on placeholders. */
    QSet<QString> m_galleryDecodeFailed;
    static constexpr int kGalleryVirtualThreshold = 80;
    static constexpr int kGalleryDecodeOverscanPx = 400;
    static constexpr int kMaxConcurrentGalleryDecodes = 12;
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
    };
    QList<PendingSessionBind> m_pendingSessionBinds;
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
    /** True while crop mode shows the full on-disk image (not the cropped pixmap). */
    bool m_cropShowingFullImage = false;
    /** Workspace free-rotate stashed while crop runs axis-aligned. */
    qreal m_cropStashedPlacementRotation = 0.0;
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
    QTimer *m_layoutDebounceTimer = nullptr;
};

#endif // IMAGEVIEW_H
