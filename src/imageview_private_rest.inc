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
    // CentreProgress: owned by HudChrome (m_hud.centreProgress()).
    /** Packaged-layout size gate (timers + pending); canvas finish via Host. */
    // Soft/display samples: ImageCache only (docs/PIXEL_HOST_CACHE.md).
    /** Session path list + parallel SessionImageId order (IDENTITY). */
    // PackOrderOverlay: owned by SessionShell (m_session.pathOrder()).

    QUndoStack *m_undoStack = nullptr;
    bool m_preserveUndoOnDestroy = false;


    ViewFraming m_framing;
    ViewShellChrome m_shell;
    ViewMode m_viewMode = ViewMode::Image;
    // SessionNavFlags: owned by ImageController (m_image).
    // CanvasBackground: owned by ViewShellChrome (m_shell.canvasBg()).
    // TextLayerSession: owned by TextLayerController (m_textCtrl).
    HudChrome m_hud;
    // SessionIdentity: owned by SessionShell (m_session.identity()).
    // Edge hover: owned by ImageController (m_image).
    // Tool: owned by WorkspaceController (m_workspace).
    // LayoutPrefs: owned by GalleryController (hostLayout).
    // GalleryDecodeBook: owned by GalleryController (hostGalleryDecodeBook).
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    // PathRasterService: owned by DisplayPipelineController (pathRaster()).
    // --- LoadAdd pending-bind helpers ---

    SessionShell m_session;
    // SessionBindBook + PendingItemAppearanceBook: m_session.bindBook().
    // ViewportChrome: owned by ViewShellChrome (m_shell.viewport()).
    // ZoomRegionGesture: owned by ImageController (m_image).


    // Group transform session: owned by WorkspaceController.

    // LayoutApplyGuard: owned by GalleryController.
    // GalleryRelayoutSuppress: owned by GalleryController.
    // Status-refresh QTimer: owned by HudChrome.
    /** Off-canvas neighbor tile prefetch (session-replace clears). */
    TileNeighborPrefetch m_tileNeighborPrefetch;
    /** BILTOO_PERF / THUMTOO_DEBUG: paint + decode-window timings. */
    PerfStats m_perf;

    // Gallery decode watchdog QTimer: owned by GalleryController.
    // Layout debounce QTimer: owned by GalleryController.
    // LayoutDebounce: owned by GalleryController.
    /** Debounce colour-slider durable write + filmstrip (see setTargetColorAdjustments). */
    // Colour-adjust commit QTimer: owned by ImageController.
    // ColorAdjustCommit: owned by ImageController (m_image).
