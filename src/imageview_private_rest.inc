// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Remainder of ImageView private: methods + data members.
// Included only from imageview.h inside class `private:`.

    // LoadRole is public on ImageView (controller TUs need it).

    /**
     * Create a canvas item from decoded pixels.
     * @p applyStoredSessionCrop — when true (normal disk decode), re-apply the
     * path's session crop from m_itemStates. When false, @p image is used as-is
     * (Workspace duplicate: pixels already include any crop/transform bake).
     */
    /** Image-mode only: content appearance for a newly created canvas item. */
    /** Interactive / gallery / static flags for the current ViewMode. */
    /**
     * If slideshow already holds pixels for @a path, queue onImageLoaded and
     * return true (skips a second disk decode under the hold).
     */
    /** Soft preview + PreferCache quality climb while slideshow is active. */
    /** Parallel thumbnail + full native decode for classic LoadReplace/LoadAdd. */
    /**
     * Image mode: install sharper ladder/PreferCache pixels in place (soft→HQ).
     * Called from onLadderReady; no-op when full native already covers.
     */
    /** SoftPreview vs FullSource from delivered sample size (not request edge). */
    /** Install/upgrade Image-mode sample; schedules PreferCache on soft. */
    /** Absolute content want for @p item (session store + live flags). */
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
    /**
     * If ImageCache has a sample with long edge ≤512, materializeDisplay
     * for @p want and attach to @p item. Returns true when attached.
     */
    /**
     * Sole attach: display pixels + layoutSize(native, want) + applied fingerprint.
     * @p display is already materializeDisplay output (or identity raw).
     */
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
    /** One ImageLoader::load when thumtoo Full settles short of on-screen need. */
    /** On-screen long edge (device px) for the current Image-mode item. */
    /** PreferCache + native full when zoom leaves soft samples undersampled. */
    /** Gallery soft state + install path for a ladderReady delivery. */
    /** PathRaster EscalateToFull for selected Workspace items. */
    /** Fit / slideshow zoom / motion handoff after Image-mode full replace. */
    /**
     * Image mode: drop the previous frame and show a loading/provisional tile
     * for @p path immediately (do not wait for the background decode).
     */
    /** Bind Image-mode item to the current session cursor (id + index). */
    /** Neutral Image-mode pose (no Workspace free-form scale/rotation). */
    /** Soft pixels for pending tile: explicit preview → slideshow map → ImageCache. */
    /** @p displayReadyOut set when soft is already content-baked (stashed Gallery tile). */
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
    /**
     * Cache-only pass before Gallery pack: fill m_sizeBook from
     * ThumtooCache::cachedSize and ImageCache from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);

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
    /** True when pack needs every aspect before the first layout (Fill modes). */
    static bool layoutDefersPopulateUntilSizes(LayoutMode mode);
    /** LQIP install + tile pyramid request for a Gallery path. */

    /** Clear stale "Improving previews" centre HUD noise. */
    /** Apply session crop from @p state to a freshly decoded item (no-op if none). */
    /**
     * Apply stored session appearance (crop + content bakes) for @p item's
     * session id / index / unbound path. Pixels must be the full on-disk image.
     */
    const WorkspaceItemState *resolveStoredAppearance(ImageItem *item,
                                                       WorkspaceItemState *fallback,
                                                       SessionImageId *sidOut);
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
    /** Record that durable appearance was probed for @p sid (hit or miss). */

    // CropHandle is defined in cropsession.h
    QRectF cropRectItemLocal() const { return m_cropCtrl.session().currentRect(); }
    qreal cropRotation() const { return m_cropCtrl.session().currentRotation(); }
    QPolygonF mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const;
    QPolygonF cropPolygonView() const;
    /** Viewport chrome button rects under the draft frame (empty when inactive). */
    CropGeometry::CropButtonLayout cropChromeLayout() const;
    bool cropAllowExpand() const { return m_cropCtrl.session().isAllowExpand(); }
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    QPointF itemLocalFromView(ImageItem *item, const QPoint &viewPos) const;
    /**
     * Image mode: keep only multiples of 90° from session state; free Workspace
     * angles map to 0°.
     */
    static qreal cardinalRotationOrZero(qreal degrees);
    /**
     * After logical size change on the Image-mode target: keep on-screen framing
     * when aspect is stable (soft→full / probe magnitude). Refit only if aspect
     * changed or size was unknown.
     */
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
    /** Handles 0–7 = scale corners/edges; 8–11 = rotate (T/R/B/L). */
    bool isGroupRotateHandle(int handle) const { return handle >= 8 && handle <= 11; }
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
     * Bound session images: content appearance is m_appearance only.
     * Path map remains Workspace free-placement cache and unbound fallback.
     */
    PathItemStateBook m_itemStateBook;
    /**
     * Phase 7 Stage 0: non-owning facade over appearance + path + size books.
     * Bound in the ImageView ctor (path/size) and bindSessionAppearance.
     */
    ItemWorld m_itemWorld;
    /**
     * Per-session-slot appearance (crop / content flip / quarter turns).
     * Keyed by session index so path duplicates stay independent value copies.
     */
    /**
     * Non-owning pointer to SessionDocument::appearance() (Tier 4b).
     * Must be set via bindSessionAppearance before appearance() is used.
     */
    SessionAppearanceStore *m_appearanceBound = nullptr;
    /** Non-owning SessionDocument (MainWindow); preferred for membership queries. */
    SessionDocument *m_sessionDoc = nullptr;
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
    QTimer *m_hudFlashTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutPrefs m_layout;
    GallerySoftBook m_gallerySoftBook;
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    PathRasterService *m_pathRaster = nullptr;
    /** @deprecated Gallery always virtualizes; kept for ABI/docs only. */
    static constexpr int kGalleryVirtualThreshold = 1;
    /** Historical Gallery concurrency knob (unused after soft removal). */
    static constexpr int kMaxConcurrentGalleryDecodes = 8;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    /** Queue of workspace restores still waiting for decode (supports same path twice). */
    /** Optional scene centre for in-flight LoadAdd decodes (e.g. drops). */
    // --- LoadAdd pending-bind helpers ---

    SessionBindBook m_bindBook;
    /** Content appearance staged by Duplicate until bindSelectedSessionIds. */
    PendingItemAppearanceBook m_pendingAppearance;
    ViewportChrome m_chrome;
    /** One-shot rubber-band zoom (Z). */
    ZoomRegionGesture m_zoomRegion;

    ItemInteractSession m_itemInteract;

    /** Image-mode focus surface (DisplaySurfaceController). Invalid outside Image. */

    /** Multi-select group scale/rotate gesture (Workspace). */
    GroupTransformSession m_groupXform;

    LayoutApplyGuard m_layoutApply;
    /** Nested suppress: Gallery delete must not repack via resizeEvent. */
    GalleryRelayoutSuppress m_galleryRelayoutSuppress;
    QTimer *m_statusRefreshTimer = nullptr;
    /** Pump tile LOD while zoomed (Image mode). */
    /** Single-shot: coalesce zoom notches before climb/tick. */
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
