// Phase 6 host accessors (controllers; prefer over friend).
// Included from ImageView public section — see REFACTOR.md.

    /** Display pipeline host: PreferCache / load path raster service. */
    PathRasterService *hostPathRaster() { return m_pathRaster; }
    const PathRasterService *hostPathRaster() const { return m_pathRaster; }
    /** Display pipeline host: slideshow controller (nav-hot, rasters, hud). */
    SlideshowController &hostSlideshow() { return m_slideshow; }
    const SlideshowController &hostSlideshow() const { return m_slideshow; }
    /** Display pipeline host: gallery soft SM book. */
    GallerySoftBook &hostGallerySoftBook() { return m_gallerySoftBook; }
    const GallerySoftBook &hostGallerySoftBook() const { return m_gallerySoftBook; }
    /** Display pipeline host: pending session binds. */
    SessionBindBook &hostBindBook() { return m_bindBook; }
    const SessionBindBook &hostBindBook() const { return m_bindBook; }
    /** Display pipeline host: layout prefs (free-form check). */
    LayoutPrefs &hostLayout() { return m_layout; }
    const LayoutPrefs &hostLayout() const { return m_layout; }
    /** Display pipeline host: gallery relayout suppress guard. */
    GalleryRelayoutSuppress &hostGalleryRelayoutSuppress() { return m_galleryRelayoutSuppress; }
    const GalleryRelayoutSuppress &hostGalleryRelayoutSuppress() const
    {
        return m_galleryRelayoutSuppress;
    }
    /** Display pipeline host: gallery controller (stashed items). */
    GalleryController &hostGallery() { return m_gallery; }
    const GalleryController &hostGallery() const { return m_gallery; }
    ImageItem *selectedOrFirstGalleryItem() const;
    void emitGalleryItemFocus(ImageItem *item);
    WorkspaceController &hostWorkspace() { return m_workspace; }
    const WorkspaceController &hostWorkspace() const { return m_workspace; }
    ItemInteractSession &hostItemInteract() { return m_itemInteract; }
    const ItemInteractSession &hostItemInteract() const { return m_itemInteract; }
    void setPageGuideSelected(bool on);
    PerfStats &hostPerf() { return m_perf; }
    const PerfStats &hostPerf() const { return m_perf; }
    LayoutApplyGuard &hostLayoutApply() { return m_layoutApply; }
    const LayoutApplyGuard &hostLayoutApply() const { return m_layoutApply; }

