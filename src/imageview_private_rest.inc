// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Remainder of ImageView private: methods + data members.
// Included only from imageview.h inside class `private:`.

    // LoadRole is public on ImageView (controller TUs need it).

    /** Native host + materialize for PNG/PDF/print (off-GUI load). */
    QImage blockingExportDisplayForItem(const ImageItem *item) const;
    void paintHighResExportItems(QPainter *painter, const QRectF &sourceScene,
                                 const QRectF &targetRect) const;
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
    void drawEdgeAffordances(QPainter &painter) { m_image.drawEdgeAffordances(painter); }
    QRectF selectionSceneBounds(const QList<ImageItem *> &items) const;
    static QSizeF nativeSize(const ImageItem *item);
    void zoomViewBy(qreal factor);

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
    /**
     * Path → native pixel size + provisional / probe-scheduled sets.
     * Dimensions are a property of the file; safe to key by path (not id).
     */
    ImageSizeCoordinator m_size;
    /** Centre HUD progress (expand / size resolve / sort). */
    CentreProgress m_centreProgress;
    /** Packaged-layout size gate (timers + pending); canvas finish via Host. */
    // Soft/display samples: ImageCache only (docs/PIXEL_HOST_CACHE.md).
    /** Session path list + parallel SessionImageId order (IDENTITY). */
    /**
     * Pack-order overlay (Tier 4 residual). Host setOrder may collapse to
     * FollowDocument when aligned; clear stays Explicit empty. See
     * packorderoverlay.h / PATH_ORDER.md.
     */
    PackOrderOverlay m_pathOrderOverlay;

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;


    ViewFraming m_framing;
    ViewMode m_viewMode = ViewMode::Image;
    SessionNavFlags m_sessionNav;
    /** Last mouse position in viewport coords (gallery hover + scroll). */
    CanvasBackground m_canvasBg;
    // TextLayerSession: owned by TextLayerController (m_textCtrl).
    HudAppearance m_hudPrefs;
    SessionIdentity m_sessionId;
    HudFlash m_hudFlash;
    QTimer *m_hudFlashTimer = nullptr;
    // Edge hover: owned by ImageController (m_image).
    // Tool: owned by WorkspaceController (m_workspace).
    // LayoutPrefs: owned by GalleryController (hostLayout).
    // GalleryDecodeBook: owned by GalleryController (hostGalleryDecodeBook).
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    PathRasterService *m_pathRaster = nullptr;
    static constexpr int kGalleryVirtualThreshold = 1;
    static constexpr int kMaxConcurrentGalleryDecodes = 8;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    // --- LoadAdd pending-bind helpers ---

    SessionBindBook m_bindBook;
    /** Content appearance staged by Duplicate until bind-on-create. */
    PendingItemAppearanceBook m_pendingAppearance;
    ViewportChrome m_chrome;
    // ZoomRegionGesture: owned by ImageController (m_image).


    /** Multi-select group scale/rotate gesture (Workspace). */

    // LayoutApplyGuard: owned by GalleryController.
    // GalleryRelayoutSuppress: owned by GalleryController.
    QTimer *m_statusRefreshTimer = nullptr;
    /** Off-canvas neighbor tile prefetch (session-replace clears). */
    TileNeighborPrefetch m_tileNeighborPrefetch;
    /** BILTOO_PERF / THUMTOO_DEBUG: paint + decode-window timings. */
    PerfStats m_perf;

    QTimer *m_galleryDecodeWatchdog = nullptr;
    QTimer *m_layoutDebounceTimer = nullptr;
    // LayoutDebounce: owned by GalleryController.
    /** Debounce colour-slider durable write + filmstrip (see setTargetColorAdjustments). */
    QTimer *m_colorAdjustCommitTimer = nullptr;
    ColorAdjustCommit m_colorAdjustCommit;
