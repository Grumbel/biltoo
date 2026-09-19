// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Remainder of ImageView private: methods + data members.
// Included only from imageview.h inside class `private:`.

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
    /** Image-mode only: content appearance for a newly created canvas item. */
    WorkspaceItemState appearanceForNewImageModeItem(const QString &path);
    /** Interactive / gallery / static flags for the current ViewMode. */
    void scheduleImageLoad(const QString &path, LoadRole role);
    /**
     * If slideshow already holds pixels for @a path, queue onImageLoaded and
     * return true (skips a second disk decode under the hold).
     */
    bool tryDeliverReplaceFromSlideshowRaster(const QString &path, quint64 gen);
    /** Soft preview + PreferCache quality climb while slideshow is active. */
    void scheduleSlideshowReplaceDecode(const QString &path, quint64 gen, LoadRole role);
    /** Parallel thumbnail + full native decode for classic LoadReplace/LoadAdd. */
    void scheduleClassicImageDecode(const QString &path, quint64 gen, LoadRole role);
    /**
     * Image mode: install sharper ladder/PreferCache pixels in place (soft→HQ).
     * Called from onLadderReady; no-op when full native already covers.
     */
    /** SoftPreview vs FullSource from delivered sample size (not request edge). */
    SessionAppearance::PixelKind pixelKindForImageModeSample(const QString &path,
                                                             const QImage &image) const;
    ImageItem *imageModeItemForPath(const QString &path) const;
    void scheduleImageModePreferCacheClimb(const QString &path, int wantEdge = 0);
    /**
     * Debounce climb + tile tick after zoom (wheel/toolbar). Avoids per-notch
     * set_viewport/issue_requests on the GUI thread during continuous zoom.
     * Pan still ticks immediately.
     */
    void scheduleTileLodAfterInteraction(int delayMs = 50);
    /** PreferCache/display edge: min(want, ladder max, known native long edge). */
    int cappedDisplayEdgeForPath(const QString &path, int wantEdge) const;
    void installImageModeSampleInPlace(ImageItem *item, const QString &path, const QImage &image,
                                       SessionAppearance::PixelKind kind);
    /** Install/upgrade Image-mode sample; schedules PreferCache on soft. */
    /** Absolute content want for @p item (session store + live flags). */
    WorkspaceItemState wantAppearanceForItem(const ImageItem *item,
                                             SessionImageId sid = kInvalidSessionImageId) const;
    bool canAcceptDisplaySample(const ImageItem *item, const QImage &pixels,
                                 SessionAppearance::PixelKind kind) const;
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
    bool applyDisplaySurfaceAction(ImageItem *item,
                                   const DisplaySurface::Action &act,
                                   const QImage &hostSample,
                                   int fallbackNeedEdge,
                                   PathRasterService::ClimbPolicy climbPolicy);
    /**
     * If ImageCache has a sample with long edge ≤512, materializeDisplay
     * for @p want and attach to @p item. Returns true when attached.
     */
    /**
     * Sole attach: display pixels + layoutSize(native, want) + applied fingerprint.
     * @p display is already materializeDisplay output (or identity raw).
     */
    void attachDisplaySample(ImageItem *item, const QImage &display,
                             const WorkspaceItemState &want,
                             SessionAppearance::PixelKind kind);
    /** Host ≤512 materialize + attach; multi-MP returns false (caller schedules). */
    bool tryRematerializeFromHost(ImageItem *item, const WorkspaceItemState &want);
    /**
     * Force contentRect / intrinsic from file-native + want (SIZE.md).
     * Soft sample dimensions must never redefine geometry when a durable size
     * is known. After live rotate/flip, always call this so paint does not
     * stretch oriented pixels into the pre-orient box.
     */
    void applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want);
    /** Worker materialize when host is multi-MP; attach on GUI if gen/want still match. */
    void scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                        const WorkspaceItemState &want);
    void finishAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                      const WorkspaceItemState &want, const QImage &display);
    bool sampleCoversNativeLogical(const QString &path, const QImage &image) const;
    void noteImageModePreferCacheDelivery(const QString &path, int requestEdge,
                                          const QImage &sample);
    void ensureImageModeQualityClimb(const QString &path, const QImage &sample);
    /** One ImageLoader::load when thumtoo Full settles short of on-screen need. */
    void scheduleImageModeNativeDecodeOnce(const QString &path);
    bool tryInstallImageModeSample(const QString &path, const QImage &image);
    bool tryInstallImageModeSampleBaked(const QString &path, const QImage &image,
                                       SessionAppearance::PixelKind kind);
    /** On-screen long edge (device px) for the current Image-mode item. */
    int imageModeOnScreenNeedEdge() const;
    /** PreferCache + native full when zoom leaves soft samples undersampled. */
    void maybeClimbImageModePixelsForView();
    void upgradeImageModeFromLadder(const QString &path, int maxEdge, const QImage &image);
    /** Gallery soft state + install path for a ladderReady delivery. */
    void applyGalleryLadderReady(const QString &path, int maxEdge, const QImage &image);
    void applyWorkspaceLadderReady(const QString &path, int maxEdge, const QImage &image);
    /** PathRaster EscalateToFull for selected Workspace items. */
    void ensureWorkspaceQualityClimb();
    void applyLegacyPathFlipsIfNeeded(ImageItem *item, const QString &path);
    /** Fit / slideshow zoom / motion handoff after Image-mode full replace. */
    void frameImageModeReplaceItem(ImageItem *item, const QString &path);
    /**
     * Image mode: drop the previous frame and show a loading/provisional tile
     * for @p path immediately (do not wait for the background decode).
     */
    void installImageModePendingTile(const QString &path, const QImage &preview = QImage());
    /** Bind Image-mode item to the current session cursor (id + index). */
    void bindImageModeSessionCursor(ImageItem *item);
    /** Neutral Image-mode pose (no Workspace free-form scale/rotation). */
    void resetImageModeItemPlacement(ImageItem *item);
    /** Soft pixels for pending tile: explicit preview → slideshow map → ImageCache. */
    QImage resolveImageModePendingPixels(const QString &path, const QImage &preview) const;
    /** @p displayReadyOut set when soft is already content-baked (stashed Gallery tile). */
    QImage resolveImageModePendingPixels(const QString &path, const QImage &preview,
                                         bool *displayReadyOut) const;
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
     * Cache-only pass before Gallery pack: fill m_sizeBook from
     * ThumtooCache::cachedSize and ImageCache from LQIP when present.
     * Does not schedule ladder encode or source I/O.
     */
    void primeGalleryGeometryFromCache(const QStringList &paths);
    bool isProvisionalImageSize(const QString &path) const;
    /** Remember native size after a successful full decode (or async probe). */
    void rememberImageSize(const QString &path, const QSize &size);
    /**
     * After a decode: prefer thumtoo/probe size. Never treat soft/ladder
     * sample dimensions as definitive logical size.
     */
    void rememberSizeFromDecode(const QString &path, const QImage &image);
    void scheduleImageSizeProbe(const QString &path);

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
    /**
     * Gallery open size probes: schedule probes for paths still missing a
     * definitive size. Returns true only when the current layout must defer
     * populate until all sizes settle (Fill / FlowFill). Grid and ordinary
     * masonry pack immediately with provisional sizes; probes still run and
     * sizeReady debounces a repack.
     */
    bool startGallerySizeResolveIfNeeded(const QStringList &paths);
    /** True when pack needs every aspect before the first layout (Fill modes). */
    static bool layoutDefersPopulateUntilSizes(LayoutMode mode);
    void noteGallerySizeProbeSettled(const QString &path);
    /** LQIP install + tile pyramid request for a Gallery path. */
    void scheduleGalleryDecode(const QString &path);

    int galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut = nullptr) const;
    /** Recover blank Gallery cells that never got LQIP installed. */
    void gallerySoftWatchdogTick();
    /** Clear stale "Improving previews" centre HUD noise. */
    void updateGallerySoftProgressHud();
    /** Bind/sync ImageFocus surface (aliases primary item surface id). */
    void ensureImageFocusSurface();
    void syncImageFocusSurfaceState();
    void driveImageFocusSurface();
    /** Bind/unbind DisplaySurface for a canvas item (Gallery/Workspace/Image). */
    void registerItemDisplaySurface(ImageItem *item);
    void unregisterItemDisplaySurface(ImageItem *item);
    /** Push live item state into the bound surface (register if needed). */
    void syncItemDisplaySurface(ImageItem *item, int hostLongEdge = -1,
                                bool climbPending = false);
    /** Ladder step for item cell size in device pixels. */
    int itemOnScreenNeedEdge(const ImageItem *item, bool allowHighRes = true) const;
    int galleryDisplayEdgeForItem(const ImageItem *item, bool allowHighRes = false) const;
    ImageItem *primaryItem() const;
    QList<ImageItem *> transformTargets() const;
    /** Apply session crop from @p state to a freshly decoded item (no-op if none). */
    /**
     * Apply stored session appearance (crop + content bakes) for @p item's
     * session id / index / unbound path. Pixels must be the full on-disk image.
     */
    const WorkspaceItemState *resolveStoredAppearance(ImageItem *item,
                                                       WorkspaceItemState *fallback,
                                                       SessionImageId *sidOut);
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
    void applyStoredContentAppearanceSeed(SessionImageId sid, const QString &path,
                                          const ThumtooCache::StoredContentAppearance &stored);
    /** Record that durable appearance was probed for @p sid (hit or miss). */
    void markAppearanceSeedAttempted(SessionImageId sid);
    /**
     * Map item-local draft rect to source pixel rect of the *current* pixmap,
     * then compose into original on-disk coordinates in @p state.
     */
    void recordSessionCrop(ImageItem *item, const QRectF &localCrop);

    // CropHandle is defined in cropsession.h
    ImageItem *cropTargetItem() const;
    ImageItem *cropSessionBoundItem() const;
    SessionImageId cropRecordSessionId(const ImageItem *item) const;
    void fitImageOrUpdateWorkspace(ImageItem *item);
    void ensureCropRectValid();
    QRectF cropRectItemLocal() const { return m_cropCtrl.session().currentRect(); }
    qreal cropRotation() const { return m_cropCtrl.session().currentRotation(); }
    /** Crop corners in item-local space (rotation about rect centre). */
    QRectF cropRectView() const;
    QPolygonF mapItemLocalPolygonToView(ImageItem *item, const QPolygonF &local) const;
    QPolygonF cropPolygonView() const;
    /** Viewport chrome button rects under the draft frame (empty when inactive). */
    CropGeometry::CropButtonLayout cropChromeLayout() const;
    bool cropAllowExpand() const { return m_cropCtrl.session().isAllowExpand(); }
    CropHandle cropHandleAt(const QPoint &viewPos) const;
    QPointF itemLocalFromView(ImageItem *item, const QPoint &viewPos) const;
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
    /** Push undo if points changed during a drag/add gesture; clear gesture state. */
    void attentionCommitSelectionMove();
    void attentionDeleteSelected();
    /** Snapshot → mutate → single undo entry (delete, detect, non-gesture edits). */
    void pushAttentionPointsUndo(const QVector<QPointF> &before,
                                 const QVector<QPointF> &after,
                                 const QString &text);
    QPointF attentionViewPos(ImageItem *item, const QPointF &norm) const;
    static void cropKeyboardMods(bool *shiftHeld, bool *ctrlHeld);
    void beginCropHandleDrag(CropHandle h, const QPoint &viewPos);
    void updateCropHandleDrag(const QPoint &viewPos);
    void endCropHandleDrag();
    bool contentLocalContains(ImageItem *item, const QPointF &local) const;
    void finishCropRubberBand();
    void beginCropRubberBand(const QPoint &viewPos);
    void updateCropRubberBand(const QPoint &viewPos);
    void endCropRubberBand();
    void leaveCropModeInternal(bool apply);
    /** Schedule thumtoo full / pool decode while crop shows a provisional sample. */
    void onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded,
                                     quint64 gen);
    void requestCropFullRaster(const QString &path);
    /** Upgrade crop source when native full arrives for m_cropCtrl.session().awaitingFullPath. */
    void maybeUpgradeCropFullRaster(const QString &path, const QImage &image);
    void pushCropAppearanceUndo(ImageItem *item, const QString &text);
    void emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                 ImageItem *item, const QImage &preferredDisplay,
                                 bool hasCrop);
    void relayoutAfterCropLeave(ImageItem *item);
    void flashCropHud(const CropFlash::Hud &hud);
    void storeCropAppearance(ImageItem *item, SessionImageId sid,
                             const WorkspaceItemState &s);
    bool applyCropCommit(ImageItem *item);
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
    bool loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const;
    void cancelPathRasterForCrop(const QString &path);
    /** Workspace: shift item so local origin (image centre) maps to @p sceneAnchor. */
    void alignItemCenterToScene(ImageItem *item, const QPointF &sceneAnchor);
    /** Cancel path: put the session crop (if any) back on the live item. */
    bool loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                    SessionImageId *sidOut) const;
    void restoreSessionCropAppearance(ImageItem *item);
    void updateMouseInfo(const QPoint &viewPos);
    /** Frame @p item in the view. Image mode: does not clear rotation/flips. */
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    /**
     * After logical size change on the Image-mode target: keep on-screen framing
     * when aspect is stable (soft→full / probe magnitude). Refit only if aspect
     * changed or size was unknown.
     */
    void preserveImageViewOnLogicalSizeChange(ImageItem *item, const QSize &before,
                                              const QSize &after);
    /**
     * Image mode: keep sceneRect tight to the target item (small fixed margin).
     * Prevents leftover Workspace/Gallery/null scene rects from allowing free or
     * asymmetric pan when the image is fitted. No-op outside Image mode.
     */
    void syncImageModeSceneRect(ImageItem *item);
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
    void updateGalleryHoverAt(const QPoint &viewPos);
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
    QList<ImageItem *> m_items;
    /**
     * Path-keyed placement / legacy unbound appearance.
     * Bound session images: content appearance is m_appearance only.
     * Path map remains Workspace free-placement cache and unbound fallback.
     */
    PathItemStateBook m_itemStateBook;
    /**
     * Per-session-slot appearance (crop / content flip / quarter turns).
     * Keyed by session index so path duplicates stay independent value copies.
     */
    /** Per-session-image appearance, keyed by stable SessionImageId. */
    /** Content appearance by stable session-image id (Phase 2 store). */
    SessionAppearanceStore m_appearance;
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
    SessionLoadGate m_loadGate;
    GallerySoftBook m_gallerySoftBook;
    /** Central path→raster climb (slideshow + shared PreferCache policy). */
    ImageModeSoftProvider m_imageModeSoftProvider;

    PathRasterService *m_pathRaster = nullptr;
    std::unique_ptr<TileLoadCoordinator> m_tileCoordinator;
    void gallerySoftResetPath(const QString &path);
    void gallerySoftResetAll();
    /** @deprecated Gallery always virtualizes; kept for ABI/docs only. */
    static constexpr int kGalleryVirtualThreshold = 1;
    /** Historical Gallery concurrency knob (unused after soft removal). */
    static constexpr int kMaxConcurrentGalleryDecodes = 8;
    /** Off-screen soft-decodes while visible work is idle (≤ free slots). */
    static constexpr int kMaxIdleGalleryDecodes = 2;
    /** Queue of workspace restores still waiting for decode (supports same path twice). */
    /** Optional scene centre for in-flight LoadAdd decodes (e.g. drops). */
    // --- LoadAdd pending-bind helpers ---
    int countPendingSessionBinds(const QString &path) const;
    void purgeSatisfiedPendingBinds(const QString &path);
    bool takePendingSessionBind(const QString &path, PendingSessionBind *out);
    void applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound);
    bool installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image);
    bool takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                          PendingSessionBind *out);
    void placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                             bool haveBound, const PendingSessionBind &bound);

    SessionBindBook m_bindBook;
    /** Content appearance staged by Duplicate until bindSelectedSessionIds. */
    PendingItemAppearanceBook m_pendingAppearance;
    ViewportChrome m_chrome;
    /** One-shot rubber-band zoom (Z). */
    ZoomRegionGesture m_zoomRegion;

    ItemInteractSession m_itemInteract;

    /** Image-mode focus surface (DisplaySurfaceController). Invalid outside Image. */
    DisplaySurfaceController m_displaySurfaces;
    DisplaySurface::SurfaceId m_imageFocusSurface = DisplaySurface::kInvalidSurfaceId;

    /** Multi-select group scale/rotate gesture (Workspace). */
    GroupTransformSession m_groupXform;

    LayoutApplyGuard m_layoutApply;
    /** Nested suppress: Gallery delete must not repack via resizeEvent. */
    GalleryRelayoutSuppress m_galleryRelayoutSuppress;
    QTimer *m_galleryDecodeScrollTimer = nullptr;
    QTimer *m_galleryStatusRefreshTimer = nullptr;
    QTimer *m_statusRefreshTimer = nullptr;
    /** Pump tile LOD while zoomed (Image mode). */
    QTimer *m_tileLodTimer = nullptr;
    /** Single-shot: coalesce zoom notches before climb/tick. */
    QTimer *m_tileLodZoomDebounce = nullptr;
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
