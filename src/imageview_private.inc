// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Private methods + data members for ImageView.
// Included only from imageview.h inside the class `private:` section.
// Do not include from other headers.

    static bool isNavEdge(EdgeZone zone)
    {
        return zone == EdgeZone::Previous || zone == EdgeZone::Next
            || zone == EdgeZone::GalleryReturn;
    }

    static EdgeZone edgeZoneFromPolicy(EdgeNavPolicy::Zone z)
    {
        switch (z) {
        case EdgeNavPolicy::Zone::Previous: return EdgeZone::Previous;
        case EdgeNavPolicy::Zone::Next: return EdgeZone::Next;
        case EdgeNavPolicy::Zone::GalleryReturn: return EdgeZone::GalleryReturn;
        case EdgeNavPolicy::Zone::None:
        default: return EdgeZone::None;
        }
    }

    static EdgeNavPolicy::Zone edgeZoneToPolicy(EdgeZone z)
    {
        switch (z) {
        case EdgeZone::Previous: return EdgeNavPolicy::Zone::Previous;
        case EdgeZone::Next: return EdgeNavPolicy::Zone::Next;
        case EdgeZone::GalleryReturn: return EdgeNavPolicy::Zone::GalleryReturn;
        case EdgeZone::None:
        default: return EdgeNavPolicy::Zone::None;
        }
    }

    /** qCritical if two live/stashed items share one SessionImageId. */
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;

    /**
     * Pack-order append (Explicit; seed from doc when promoting out of FollowDocument).
     * Place/add via hostPathOrderAppendRow (WorkspaceController).
     */
    void pathOrderAppendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_session.pathOrder().appendExplicitRow(path, id, m_sessionDoc);
    }

    /** Stage 2: read live ImageItem pose into a Placement record. */
    static ItemComponents::Placement placementFromItem(const ImageItem *item);
    /**
     * Apply pose after geometry undo/redo so live item + ItemWorld Placement align.
     */
    void applyGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl);
    /** Write Placement (or path-book pose) without requiring a full DTO snapshot. */
    void persistGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl);





    /**
     * Choose live tiles to remove when setWorkspacePaths rebuilds membership.
     * Prefer SessionImageId; path occurrence counts only for unbound leftovers.
     */
    QList<ImageItem *> collectDoomedWorkspaceItems(const QStringList &paths,
                                                   const QVector<SessionImageId> &sessionIds) const;



    ImageItem *findItemBySessionIndex(int sessionIndex) const;

    QList<ImageItem *> collectItemsForSessionId(SessionImageId sessionId) const;

    QStringList destroySessionIdItems(const QList<ImageItem *> &doomed);

    void prunePendingBindsAndSavedForSessionId(SessionImageId sessionId);

    void prunePathOrdersAfterSessionRemove(const QStringList &removedPaths);

    void restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                           const QPointF &keptCenter, int scrollH, int scrollV);

    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;

    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;

    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    /**
     * Coalesce rapid Gallery packs (e.g. sizeReady storm when opening a large
     * archive). Runs applyLayout after a short idle; Enter/Explicit callers
     * should still use applyLayout directly.
     */

    void persistSessionAppearanceSlot(ImageItem *item);



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

    /** Interactive grade: clamped host bake (no SQLite / filmstrip). */

    /** After slider idle: durable save, filmstrip, full rematerialize if needed. */


    SessionImageId resolveContentEditSessionId(const ImageItem *item) const;


    void appendThumtooDebugStatus(QString *text, ImageItem *item) const;

    QString statusTextEmpty() const;

    QString statusTextMultiItem(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;


    QString statusTextImageMode(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;

    /** User-facing quality of pixels currently shown for @p item. */
    QString pixelQualityLabel(const ImageItem *item) const;

    /** Basename of the current/target image for the bottom HUD. */

    // --- Paint / input phases (were public via host_pipeline mid-include) ---

    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);
    /** Gallery multi-select frames in scene space (not inside item paint cache). */
    /**
     * Map a page text/link region into the primary item's current image-pixel
     * space (after session content flip / quarter-turn bake). Empty if the
     * layer or item is unavailable.
     */
    /** Map viewport rubber rect → image-pixel rect on primary item. */
    /** Hit-test link region under view pos; sets page/uri outs. */
    [[nodiscard]] bool hitTextLinkAt(const QPoint &viewPos, int *pageOut,
                                    QString *uriOut) const;

    void drawBackground(QPainter *painter, const QRectF &rect) override;
    /** Scene-space canvas background (AppDefault or Workspace override). */
    void paintCanvasBackground(QPainter *painter, const QRectF &rect, qreal viewScale);
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    // Geometry undo (also host surface for controllers)
    void pushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                               const ItemComponents::Placement &after, const QString &text);


// --- private remainder (members + late private methods) ---

    // LoadRole is public on ImageView (controller TUs need it).

    /** Native host + materialize for PNG/PDF/print (off-GUI load). */
    QImage blockingExportDisplayForItem(const ImageItem *item) const;
    /**
     * Best-known native pixel size for @p path from the session cache only.
     * Never does filesystem I/O — returns a neutral size and schedules an async
     * probe when unknown (slow network/USB must not block the GUI thread).
     */
    QSize imageSizeForPath(const QString &path);
    /**
     * Cache-only pass before Gallery pack: fill size book from
     * ThumtooCache::cachedSize and ImageCache from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);

    // --- TileNeighborPrefetchHost ------------------------------------------
    bool pathOnLiveCanvas(const QString &path) const override;
    QSize viewportWidgetSize() const override
    {
        if (const QWidget *vp = viewport()) {
            return vp->size();
        }
        return {};
    }
    qreal prefetchDevicePixelRatio() const override
    {
        if (const QWidget *vp = viewport()) {
            return vp->devicePixelRatioF();
        }
        return 1.0;
    }
    bool tilePrefetchNavHot() const override
    {
        return m_slideshow.hud().isNavHot();
    }

    /** True when pack needs every aspect before the first layout (Fill modes). */
    static bool layoutDefersPopulateUntilSizes(LayoutMode mode);
    const WorkspaceItemState *resolveStoredAppearance(ImageItem *item,
                                                       WorkspaceItemState *fallback,
                                                       SessionImageId *sidOut);
    /**
     * Image mode: keep only multiples of 90° from session state; free Workspace
     * angles map to 0°.
     */
    static qreal cardinalRotationOrZero(qreal degrees);
    /**
     * Image mode: keep sceneRect tight to the target item (small fixed margin).
     * Prevents leftover Workspace/Gallery/null scene rects from allowing free or
     * asymmetric pan when the image is fitted. No-op outside Image mode.
     */
    void ensureVisibleItem(ImageItem *item);
    void rememberItemState(ImageItem *item);
    WorkspaceItemState defaultStateForPath(const QString &path, int ordinal) const;
    /** Centre position that does not overlap existing items (viewport-aware). */
    QPointF findEmptyPlacement(const QSizeF &itemSize) const;
    int edgeZoneWidth() const { return m_image.edgeZoneWidth(); }
    int edgeZoneHeight() const { return m_image.edgeZoneHeight(); }
    bool setHoverEdge(EdgeZone zone);
    void clearHoverEdge() { m_image.clearHoverEdge(); }
    void updateHoverEdge(const QPoint &viewPos) { m_image.updateHoverEdge(viewPos); }
    QRectF selectionSceneBounds(const QList<ImageItem *> &items) const;
    static QSizeF nativeSize(const ImageItem *item);

    QGraphicsScene *m_scene = nullptr;
    /** Gallery-mode collaborator (stash, viewport snapshot, transitions). */
    GalleryController m_gallery;
    /** Phase 6 Tier 1: slideshow state (phase/dwell/HUD/settings/ZoomBlur/motion). */
    SlideshowController m_slideshow;
    /** Phase 6 Tier 2a: crop/attention session state on controllers. */
    CropController m_cropCtrl;
    AttentionController m_attentionCtrl;
    /** Workspace-mode collaborator (stash, free-form snapshot, transitions). */
    WorkspaceController m_workspace;
    /** Image-mode collaborator (enter transition). */
    ImageController m_image;
    /** Page text / link overlay (search, rubber-band). */
    TextLayerController m_textCtrl;
    /**
     * Phase 6 Tier 5a / Dual Stage 2c.1: display pipeline.
     * Default: owned unique_ptr. Dual pane: secondary host binds external via
     * bindSharedDisplayPipeline() and releases ownership of m_ownedPipeline.
     */
    std::unique_ptr<DisplayPipelineController> m_ownedPipeline;
    /** Non-null after ctor; points at owned or shared external pipeline. */
    DisplayPipelineController *m_displayPipeline = nullptr;
    QList<ImageItem *> m_items;
    /**
     * Path-keyed placement / legacy unbound appearance.
     * Bound session images: content appearance is ItemWorld sparse tables only.
     * Path map remains Workspace free-placement cache and unbound fallback.
     */
    /** Unbound content + placement (bound content is ItemWorld sparse). */
    PathItemStateBook m_pathStateBook;
    /**
     * Phase 7 Stage 0: owned facade over appearance + path + size books.
     * Bound in the ImageView ctor (path/size) and bindSessionSeedBook.
     * Dual ImageView: optional external world via bindSharedItemWorld().
     */
    ItemWorld m_itemWorld;
    /** Non-owning; when set, itemWorld() returns *m_sharedItemWorld. */
    ItemWorld *m_sharedItemWorld = nullptr;
    SessionSeedBook *m_seedBook = nullptr;
    /** Non-owning SessionDocument (MainWindow); preferred for membership queries. */
    SessionDocument *m_sessionDoc = nullptr;
    FilmstripSelectionProvider m_filmstripSelectionProvider;
    /**
     * Path → native pixel size + provisional / probe-scheduled sets.
     * Dimensions are a property of the file; safe to key by path (not id).
     */
    ImageSizeCoordinator m_size;
    // Soft/display samples: ImageCache only (docs/PIXEL_HOST_CACHE.md).

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;


    // ViewFraming: owned by ImageController (m_image.framing()).
    /** Viewport pointer chrome + canvas materials. */
    ViewShellChrome m_shell;
    ViewMode m_viewMode = ViewMode::Image;
    /** HUD appearance, flash, centre progress, perf overlay, status-refresh timer. */
    HudChrome m_hud;
    /** Soft-provider injection (dual shell / MainWindow). */
    ImageModeSoftProvider m_imageModeSoftProvider;
    /** Session identity, LoadAdd bind queue, pack-order overlay. */
    SessionShell m_session;
    /**
     * Off-canvas neighbor tile prefetch (per-view: pathOnLiveCanvas for dual pane).
     * Not shared on DisplayPipelineController.
     */
    TileNeighborPrefetch m_tileNeighborPrefetch;
