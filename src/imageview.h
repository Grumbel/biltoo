// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H
#include "imageview_types.h"
#include "gallery/gallerysizeresolve.h"
#include "display/tileneighborprefetch.h"
#include "display/displaypipelinehost.h"
#include "crop/cropsession.h"
#include "crop/cropgeometry.h"
#include "crop/cropflash.h"
#include "attention/attentionsession.h"
#include "shell/centreprogress.h"
#include "workspace/grouptransformsession.h"
#include "workspace/pageguidesession.h"
#include "item/iteminteractsession.h"
#include "view/viewframing.h"
#include "text/textlayercontroller.h"
#include "slideshow/zoomregiongesture.h"
#include "gallery/layoutprefs.h"
#include "view/viewshellchrome.h"
#include "hud/hudchrome.h"
#include "session/sessionshell.h"
#include "gallery/gallerydecodebook.h"
#include "util/perfstats.h"
#include "color/coloradjustcommit.h"
#include "gallery/layoutdebounce.h"
#include "gallery/galleryrelayoutsuppress.h"
#include "gallery/layoutapplyguard.h"
#include "item/imagesizebook.h"
#include "item/imagesizecoordinator.h"
#include "item/pathitemstatebook.h"
#include "item/pendingitemappearancebook.h"
#include "slideshow/slideshowtypes.h"
#include "slideshow/motionscrollchrome.h"
#include "display/loadgeneration.h"
#include "session/sessionloadgate.h"
#include "session/sessionpathorder.h"
#include "session/packorderview.h"
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
                  private TileNeighborPrefetchHost,
                  public DisplayPipelineHost
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

    /**
     * Workspace: place @p path at @p scenePos for @p sessionId. If that session
     * image is already on the canvas, move it; otherwise create a bound tile.
     */

    /**
     * Session list order for @p item. Prefers SessionDocument::indexOfId when
     * the item is bound; falls back to the cached ImageItem::sessionIndex().
     * List position is not identity (IDENTITY.md) — use sessionId for that.
     */
    int sessionListIndex(const ImageItem *item) const override;
    /**
     * Stamp list-order cache from document when bound; clear to -1 when unbound.
     * Pack-row hints must be restamped by the caller after refresh.
     * Stage 2 residual: call after setSessionId so the cache cannot lag
     * document order. Returns the stamped index, or -1 when unbound/unknown.
     */
    int refreshSessionIndexCache(ImageItem *item) override;
    // Load roles (public for DisplayPipelineController; not under public slots — moc).
    enum LoadRole {
        LoadReplace = 0,
        LoadAdd = 1,
        LoadRestore = 2
    };

    bool isMultiItemMode() const override { return m_viewMode != ViewMode::Image; }
    /** Gallery empty-canvas press: rubber-band via QGraphicsView base. */
    void forwardGraphicsViewMousePress(QMouseEvent *event)
    { QGraphicsView::mousePressEvent(event); }
    // =====================================================================
    // Mode-controller host API
    // Used by ImageController / GalleryController / WorkspaceController.
    // Prefer these over reaching into ImageView internals.
    // =====================================================================
    /** Controller host: set m_viewMode + hostLayout().currentMode() and refresh viewport. */
    void setActiveMode(ViewMode mode, LayoutMode layout);
    /** Open/History session barrier: bump gen, clear canvas, cancel thumtoo. */
    void invalidateSessionLoads();
    /** Controller host: LoadReplace for a path (no-op if empty). */
    void scheduleReplaceLoad(const QString &path);
    /** Controller host: live canvas item list. */
    QList<ImageItem *> &liveItems() override { return m_items; }
    const QList<ImageItem *> &liveItems() const override { return m_items; }
    QGraphicsScene *canvasScene() override { return m_scene; }
    /** Controller host: applyItemModeFlags to every live item. */
    void applyModeFlagsToLiveItems();
    /**
     * Logical image size for @a path (never soft-raster dimensions).
     * Lookup only: size book, then thumtoo cache. Empty if unknown.
     * Slideshow and Image-mode framing share this.
     */
    QSize logicalSizeForPath(const QString &path) const override;
    /**
     * Content layout size for a session row (or unbound path): native file size
     * × ItemWorld content ops (SessionImageId). Same rule as filmstrip provider
     * and applyContentLayoutSize — never soft sample dims, never native alone
     * when orient/crop is known.
     */
    QSize contentLayoutSize(const QString &path, SessionImageId sessionId,
                            bool allowStoreAppearance = true) const override;
    // Host accessors (controllers): path raster, books, prefs — imageview_host_accessors.inc
#include "imageview_host_accessors.inc"

    /**
     * Phase 7 Stage 0 facade (appearance / path-book / size-book).
     * Pack-order host mutators live in imageview_host_pipeline.inc
     * (pathOrderClear / SetOrder / currentPackOrder; AppendRow is private).
     */
    ItemWorld &itemWorld() override
    {
        return m_sharedItemWorld ? *m_sharedItemWorld : m_itemWorld;
    }
    const ItemWorld &itemWorld() const override
    {
        return m_sharedItemWorld ? *m_sharedItemWorld : m_itemWorld;
    }
    /**
     * Dual ImageView Stage 2c: share durable appearance across hosts.
     * @p world non-null — host methods use that world (must already bind
     * path/size books). @p world null — revert to this view's owned ItemWorld.
     * Does not transfer ownership.
     */
    void bindSharedItemWorld(ItemWorld *world) { m_sharedItemWorld = world; }
    /** Non-null when this host is using an external shared ItemWorld. */
    ItemWorld *sharedItemWorld() const { return m_sharedItemWorld; }
    /**
     * Dual ImageView Stage 2c.1: share one DisplayPipelineController across hosts.
     * Releases any owned pipeline and points at @p pipeline (non-owning).
     * Caller remains the lifetime owner. Does not call setActiveHost — the shell
     * should setActiveHost on focus change.
     */
    void bindSharedDisplayPipeline(DisplayPipelineController *pipeline);
    /** Non-null when pipeline is external (not m_ownedPipeline). */
    bool hasSharedDisplayPipeline() const
    {
        return m_displayPipeline && m_displayPipeline != m_ownedPipeline.get();
    }
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
    SessionDocument *sessionDocument() const override { return m_sessionDoc; }
    void takePendingWorkspacePath(const QString &path) override;


// Crop/display ops — imageview_host_crop_display.inc
#include "imageview_host_crop_display.inc"
// Mode/canvas ops (find/destroy/gallery/page-guide) — imageview_host_ops.inc
#include "imageview_host_ops.inc"

    void setViewMode(ViewMode mode);


    /** Reset view/scene so Image mode is not affected by prior canvas state. */
    void prepareImageModeCanvas() override;
    bool isImageMode() const override { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const override { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const override { return m_viewMode == ViewMode::Workspace; }
    /** DisplayPipelineHost: QGraphicsView viewport for update/geometry. */
    QWidget *viewportWidget() override { return viewport(); }
    QObject *hostObject() override { return this; }
    void notifyStatusChanged() override { emit statusChanged(); }
    void notifyWorkspacePathsChanged() override { emit workspacePathsChanged(); }
    /** Gallery size gate finished (GalleryController host path). */
    void notifyGallerySizeResolveFinished() { emit gallerySizeResolveFinished(); }
    void setUpdatesEnabled(bool enabled) override { QGraphicsView::setUpdatesEnabled(enabled); }
    QString hostTr(const char *sourceText) const override { return tr(sourceText); }
    qreal devicePixelRatioF() const override { return QGraphicsView::devicePixelRatioF(); }
    // Keep QRect / QPolygonF overloads visible (DisplayPipelineHost only needs QPointF).
    using QGraphicsView::mapFromScene;
    QPointF mapFromScene(const QPointF &point) const override { return QGraphicsView::mapFromScene(point); }
    // Keep QRect / QPolygonF overloads visible (DisplayPipelineHost only needs QPoint).
    using QGraphicsView::mapToScene;
    QPointF mapToScene(const QPoint &point) const override { return QGraphicsView::mapToScene(point); }
    QTransform viewTransform() const override { return QGraphicsView::transform(); }
    QRectF mapViewportToScene() const override
    {
        QWidget *vp = viewport();
        if (!vp) {
            return QRectF();
        }
        return QGraphicsView::mapToScene(vp->rect()).boundingRect();
    }



    /**
     * Show exactly the given session rows on the workspace. @p sessionIds must
     * align with @p paths (one id per path). Images already present keep their
     * live transform; newly added ones restore saved state or get a default
     * placement. Images no longer listed are removed after their state is saved.
     * Path-only rebuild is removed — callers pass session ids so duplicate
     * paths stay distinct (IDENTITY.md).
     */

    /**
     * Reorder canvas items to match session/pack order. When @p ids align with
     * @p paths, prefer findItemBySessionId so duplicate paths stay distinct
     * (IDENTITY). Path first-unseen is fallback for unbound rows only.
     */
    void reorderItemsByPaths(const QStringList &paths,
                             const QVector<SessionImageId> &ids = {}) override;


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
    Tool currentTool() const { return m_workspace.currentTool(); }


    /**
     * Session position for status line and HUD (index/total, 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     * @p pulseIdentity briefly shows filename/badge without a pinned HUD (H);
     * use false for silent updates (e.g. slideshow auto-advance).
     */
    void setCurrentSessionId(SessionImageId id) override;

    // Slideshow dwell/timeline/phase/Ken Burns/pause cues: SlideshowController
    // (hostSlideshow()). Not re-exported on ImageView.
    // Sticky zoom / region zoom / rotate / duplicate: hostImage() / hostWorkspace().

    void setLayoutMode(LayoutMode mode);


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
    void applyState(ImageItem *item, const WorkspaceItemState &state) override;
    /**
     * Install applied ContentXform fingerprint on the live ImageItem.
     * ItemWorld remains authority for bound ids. Applied survives pixel clear.
     */
    void syncLiveContentMetaFromState(ImageItem *item, const WorkspaceItemState &state) override;
    /**
     * Install live color grade on the item (record-only or rebuild display).
     * ItemWorld Color table remains authority for bound ids.
     */
    void syncLiveColorFromState(ImageItem *item, const ColorAdjustments &grade,
                                bool rebuildDisplay = false) override;
    /**
     * Identity clear: drop applied ContentXform fingerprint.
     */
    void clearLiveContentMeta(ImageItem *item) override;
    /**
     * Applied ContentXform fingerprint for @p item.
     * Prefer ItemWorld runtime table when bound (Stage 2 residual 2081);
     * fall back to ImageItem mirror (paint / unbound).
     */
    bool itemHasAppliedContentXform(const ImageItem *item) const override;
    ContentXform::Value itemAppliedContentXform(const ImageItem *item) const override;
    /**
     * Live colour grade for @p item (slider / paint lag).
     * Prefer ItemWorld runtime lag when bound (host-side scratch); ImageItem
     * mirror for paint / unbound. Durable grade is ItemWorld Color.
     */
    ColorAdjustments itemLiveColor(const ImageItem *item) const override;
    void setItemSessionId(ImageItem *item, SessionImageId id) override;
    void setItemSessionIndex(ImageItem *item, int index);
    /** Persist session state and refresh filmstrip (chrome / toolbar edits). */
    void commitItemSessionEdit(ImageItem *item) override;
    /** Copy of stored appearance for @p id (empty/default if none). */
    /**
     * Bound appearance for @p id (ItemWorld::appearanceValue — sparse-prefer).
     */
    WorkspaceItemState sessionAppearanceValue(SessionImageId id) const override;


    QString statusText() const;
    /** Basename of the current/target image for the bottom HUD. */
    QString hudFileName() const;
    /** Dedicated HUD line: Loading · N active · cache vs file/archive. */
    QString loadingStatusHudLine() const;
    QSize imageSize() const;
    int itemCount() const override;
    QStringList itemPaths() const;
    /** Live canvas SessionImageIds (parallel to itemPaths; invalid when unbound). */
    QVector<SessionImageId> itemSessionIds() const;
    /** Paths of selected canvas items (Gallery/Workspace). Image mode: current path. */
    QStringList selectedPaths() const;
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
