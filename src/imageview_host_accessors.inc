// Phase 6 host accessors — bag/controller references only (prefer over friend).
// Operations: host_ops / host_crop_display / host_pipeline. Included from ImageView public.

    /** Display pipeline host: PreferCache / load path raster service. */
    PathRasterService *hostPathRaster() override { return m_pathRaster; }
    const PathRasterService *hostPathRaster() const override { return m_pathRaster; }
    /** Image-mode controller (classic path, enter, reload). */
    ImageController &hostImage() override { return m_image; }
    const ImageController &hostImage() const override { return m_image; }
    /** Display pipeline host: slideshow controller (nav-hot, rasters, hud). */
    SlideshowController &hostSlideshow() override { return m_slideshow; }
    const SlideshowController &hostSlideshow() const override { return m_slideshow; }
    /** Display pipeline host: gallery soft SM book (owned by GalleryController). */
    GalleryDecodeBook &hostGalleryDecodeBook() override { return m_gallery.decodeBook(); }
    const GalleryDecodeBook &hostGalleryDecodeBook() const override { return m_gallery.decodeBook(); }
    /** Display pipeline host: pending session binds. */
    SessionBindBook &hostBindBook() override { return m_bindBook; }
    const SessionBindBook &hostBindBook() const override { return m_bindBook; }
    /**
     * Dual ImageView: secondary hosts that bindSharedItemWorld must use the
     * shared world's size book (primary-owned). Local m_size.book() stays empty
     * otherwise layout stays 1×1 and the compare pane paints blank.
     */
    ImageSizeBook &hostSizeBook() override
    {
        if (m_sharedItemWorld && m_sharedItemWorld->hasSizeBookBound()) {
            return m_sharedItemWorld->sizeBook();
        }
        return m_size.book();
    }
    const ImageSizeBook &hostSizeBook() const override
    {
        if (m_sharedItemWorld && m_sharedItemWorld->hasSizeBookBound()) {
            return m_sharedItemWorld->sizeBook();
        }
        return m_size.book();
    }
    ViewFraming &hostFraming() override { return m_framing; }
    const ViewFraming &hostFraming() const override { return m_framing; }
    ViewportChrome &hostChrome() { return m_chrome; }
    const ViewportChrome &hostChrome() const { return m_chrome; }
    /** Print page-guide session (Workspace overlay). */
    PageGuideSession &hostPageGuide() { return m_workspace.pageGuideSession(); }
    const PageGuideSession &hostPageGuide() const { return m_workspace.pageGuideSession(); }
    GallerySizeResolve &hostGallerySizeResolve() override { return m_gallery.sizeResolve(); }
    const GallerySizeResolve &hostGallerySizeResolve() const override { return m_gallery.sizeResolve(); }
    /** Display pipeline host: layout prefs (free-form check). */
    LayoutPrefs &hostLayout() override { return m_layout; }
    const LayoutPrefs &hostLayout() const override { return m_layout; }
    /** Display pipeline host: gallery relayout suppress guard. */
    GalleryRelayoutSuppress &hostGalleryRelayoutSuppress() override { return m_galleryRelayoutSuppress; }
    const GalleryRelayoutSuppress &hostGalleryRelayoutSuppress() const override
    {
        return m_galleryRelayoutSuppress;
    }
    /** Display pipeline host: gallery controller (stashed items). */
    GalleryController &hostGallery() override { return m_gallery; }
    const GalleryController &hostGallery() const override { return m_gallery; }
    WorkspaceController &hostWorkspace() override { return m_workspace; }
    const WorkspaceController &hostWorkspace() const override { return m_workspace; }
    ItemInteractSession &hostItemInteract() { return m_workspace.itemInteract(); }
    const ItemInteractSession &hostItemInteract() const { return m_workspace.itemInteract(); }
    PerfStats &hostPerf() { return m_perf; }
    const PerfStats &hostPerf() const { return m_perf; }
    LayoutApplyGuard &hostLayoutApply() { return m_layoutApply; }
    const LayoutApplyGuard &hostLayoutApply() const { return m_layoutApply; }
    /** Gallery pack debounce single-shot timer (may be null until first use). */
    QTimer *&hostLayoutDebounceTimer() { return m_layoutDebounceTimer; }
    QTimer *hostLayoutDebounceTimer() const { return m_layoutDebounceTimer; }
    CentreProgress &hostCentreProgress() { return m_centreProgress; }
    const CentreProgress &hostCentreProgress() const { return m_centreProgress; }
    QUndoStack *hostUndoStack() { return m_undoStack; }
    const QUndoStack *hostUndoStack() const { return m_undoStack; }
    DisplayPipelineController &hostDisplayPipeline()
    {
        Q_ASSERT(m_displayPipeline);
        return *m_displayPipeline;
    }
    const DisplayPipelineController &hostDisplayPipeline() const
    {
        Q_ASSERT(m_displayPipeline);
        return *m_displayPipeline;
    }

    /** Display pipeline host: crop controller (draft freeze). */
    CropController &hostCrop() override { return m_cropCtrl; }
    const CropController &hostCrop() const override { return m_cropCtrl; }
    AttentionController &hostAttention() { return m_attentionCtrl; }
    const AttentionController &hostAttention() const { return m_attentionCtrl; }
    /** Display pipeline host: text layer (loadImage side effects). */
    TextLayerSession &hostTextLayer() override { return m_textLayer; }
    const TextLayerSession &hostTextLayer() const override { return m_textLayer; }
    /** Display pipeline host: filmstrip soft provider for image-mode pending. */
    ImageModeSoftProvider hostImageModeSoftProvider() const override { return m_imageModeSoftProvider; }

    SessionSeedBook &hostSeedBook() override
    {
        Q_ASSERT(m_seedBook);
        return *m_seedBook;
    }
    const SessionSeedBook &hostSeedBook() const override
    {
        Q_ASSERT(m_seedBook);
        return *m_seedBook;
    }
    CanvasBackground &hostCanvasBg() { return m_canvasBg; }
    const CanvasBackground &hostCanvasBg() const { return m_canvasBg; }
    SessionIdentity &hostSessionId() override { return m_sessionId; }
    const SessionIdentity &hostSessionId() const override { return m_sessionId; }
    /**
     * First SessionImageId for @p path from the MainWindow SessionDocument when
     * bound (slideshow / identity). View path-order book is multiplicity only —
     * use pathOrderOccurrences for LoadAdd counts, not this.
     */
    SessionImageId firstSessionIdForPath(const QString &path) const
    {
        if (m_sessionDoc) {
            return m_sessionDoc->firstIdForPath(path);
        }
        return currentPackOrder().firstIdForPath(path);
    }
    PathItemStateBook &hostItemStateBook() { return m_itemWorld.pathBook(); }
    const PathItemStateBook &hostItemStateBook() const { return m_itemWorld.pathBook(); }
    HudAppearance &hostHudPrefs() { return m_hudPrefs; }
    const HudAppearance &hostHudPrefs() const { return m_hudPrefs; }
    HudFlash &hostHudFlash() { return m_hudFlash; }
    const HudFlash &hostHudFlash() const { return m_hudFlash; }
    QTimer *&hostHudFlashTimer() { return m_hudFlashTimer; }
    QTimer *hostHudFlashTimer() const { return m_hudFlashTimer; }
    DisplaySurfaceController &hostDisplaySurfaces() { return m_displayPipeline->displaySurfaces(); }
    const DisplaySurfaceController &hostDisplaySurfaces() const { return m_displayPipeline->displaySurfaces(); }
    TileNeighborPrefetch &hostTileNeighborPrefetch() override { return m_tileNeighborPrefetch; }
    const TileNeighborPrefetch &hostTileNeighborPrefetch() const override { return m_tileNeighborPrefetch; }
    EdgeZone hostHoverEdge() const { return m_hoverEdge; }
    SessionLoadGate &hostLoadGate() { return m_displayPipeline->loadGate(); }
    const SessionLoadGate &hostLoadGate() const { return m_displayPipeline->loadGate(); }
