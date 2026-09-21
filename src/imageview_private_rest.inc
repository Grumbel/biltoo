// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Remainder of ImageView private: methods + data members.
// Included only from imageview.h inside class `private:`.

    // LoadRole is public on ImageView (controller TUs need it).

    /** Native host + materialize for PNG/PDF/print (off-GUI load). */
    QImage blockingExportDisplayForItem(const ImageItem *item) const;
    void paintHighResExportItems(QPainter *painter, const QRectF &sourceScene,
                                 const QRectF &targetRect) const;
    /** Host ≤512 materialize + attach; multi-MP returns false (caller schedules). */
    bool tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want);
    /**
     * Force contentRect / intrinsic from file-native + want (SIZE.md).
     * Soft sample dimensions must never redefine geometry when a durable size
     * is known. After live rotate/flip, always call this so paint does not
     * stretch oriented pixels into the pre-orient box.
     */
    void finishAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                      const WorkspaceItemState &want, const QImage &display);
    /**
     * Best-known native pixel size for @p path from the session cache only.
     * Never does filesystem I/O — returns a neutral size and schedules an async
     * probe when unknown (slow network/USB must not block the GUI thread).
     */
    QSize imageSizeForPath(const QString &path);
    /**
     * Cache-only pass before Gallery pack: fill m_sizeBook from
     * ThumtooCache::cachedSize and ImageCache from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);

    // --- GallerySizeResolveHost -------------------------------------------------
    bool hasDefinitiveHostSize(const QString &path) const override
    {
        return m_sizeBook.hasDefinitive(path);
    }
    void adoptResolvedSize(const QString &path, const QSize &size) override;
    void scheduleSizeProbe(const QString &path) override
    {
        scheduleImageSizeProbe(path);
    }
    /** Pack order for TTFP probes — overlay resolve (Explicit book). */
    QStringList sizeResolvePathOrder() const override
    {
        return currentPackOrder().paths();
    }
    bool sizeResolveLayoutDefersPopulate() const override
    {
        return layoutDefersPopulateUntilSizes(m_layout.currentMode());
    }
    void setSizeResolveProgress(const QString &title,
                                 const QString &detail) override
    {
        setCentreProgress(title, detail);
    }
    void clearSizeResolveProgress() override;
    void onSizeResolveGateComplete() override;
    void onSizeResolveGateCancelled() override;

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

    void applyProbedImageSize(const QString &path, const QSize &size);
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
    qreal angleAt(const QPointF &scenePos, ImageItem *item) const;
    void rememberItemState(ImageItem *item);
    WorkspaceItemState defaultStateForPath(const QString &path, int ordinal) const;
    /** Centre position that does not overlap existing items (viewport-aware). */
    QPointF findEmptyPlacement(const QSizeF &itemSize) const;
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
    void updateGroupRotate(const QPointF &scenePos, Qt::KeyboardModifiers mods);
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
    /** Phase 6 Tier 5a: load gate, surfaces, tile LOD timers/coordinator. */
    DisplayPipelineController m_displayPipeline;
    QList<ImageItem *> m_items;
    /**
     * Path-keyed placement / legacy unbound appearance.
     * Bound session images: content appearance is ItemWorld sparse tables only.
     * Path map remains Workspace free-placement cache and unbound fallback.
     */
    PathItemStateBook m_itemStateBook;
    /**
     * Phase 7 Stage 0: non-owning facade over appearance + path + size books.
     * Bound in the ImageView ctor (path/size) and bindSessionSeedBook.
     */
    ItemWorld m_itemWorld;
    SessionSeedBook *m_seedBook = nullptr;
    /** Non-owning SessionDocument (MainWindow); preferred for membership queries. */
    SessionDocument *m_sessionDoc = nullptr;
    /**
     * Path → native pixel size + provisional / probe-scheduled sets.
     * Dimensions are a property of the file; safe to key by path (not id).
     */
    ImageSizeBook m_sizeBook;
    /** Centre HUD progress (expand / size resolve / sort). */
    CentreProgress m_centreProgress;
    /** Packaged-layout size gate (timers + pending); canvas finish via Host. */
    GallerySizeResolve m_gallerySizeResolve;
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
    /** Last mouse position in viewport coords (gallery hover + scroll). */
    CanvasBackground m_canvasBg;
    TextLayerSession m_textLayer;
    HudAppearance m_hudPrefs;
    SessionIdentity m_sessionId;
    HudFlash m_hudFlash;
    QTimer *m_hudFlashTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutPrefs m_layout;
    GallerySoftBook m_gallerySoftBook;
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    PathRasterService *m_pathRaster = nullptr;
    static constexpr int kGalleryVirtualThreshold = 1;
    static constexpr int kMaxConcurrentGalleryDecodes = 8;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    // --- LoadAdd pending-bind helpers ---

    SessionBindBook m_bindBook;
    /** Content appearance staged by Duplicate until bindSelectedSessionIds. */
    PendingItemAppearanceBook m_pendingAppearance;
    ViewportChrome m_chrome;
    /** One-shot rubber-band zoom (Z). */
    ZoomRegionGesture m_zoomRegion;

    ItemInteractSession m_itemInteract;

    /** Multi-select group scale/rotate gesture (Workspace). */
    GroupTransformSession m_groupXform;

    LayoutApplyGuard m_layoutApply;
    /** Nested suppress: Gallery delete must not repack via resizeEvent. */
    GalleryRelayoutSuppress m_galleryRelayoutSuppress;
    QTimer *m_statusRefreshTimer = nullptr;
    /** Off-canvas neighbor tile prefetch (session-replace clears). */
    TileNeighborPrefetch m_tileNeighborPrefetch;
    /** BILTOO_PERF / THUMTOO_DEBUG: paint + decode-window timings. */
    PerfStats m_perf;

    QTimer *m_gallerySoftWatchdog = nullptr;
    QTimer *m_layoutDebounceTimer = nullptr;
    LayoutDebounce m_layoutDebounce;
    /** Debounce colour-slider durable write + filmstrip (see setTargetColorAdjustments). */
    QTimer *m_colorAdjustCommitTimer = nullptr;
    ColorAdjustCommit m_colorAdjustCommit;
