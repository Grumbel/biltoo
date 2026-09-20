// Crop / slideshow / display host surface (Phase 6 Tier 2b / 5c).
// Included from ImageView public section.

    // --- Crop host (Phase 6 Tier 2b CropController) ---
    void storeCropAppearance(ImageItem *item, SessionImageId sid,
                             const WorkspaceItemState &s);
    bool loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                    SessionImageId *sidOut) const;
    void rememberSizeFromDecode(const QString &path, const QImage &image);
    void rememberImageSize(const QString &path, const QSize &size);
    bool sampleCoversNativeLogical(const QString &path, const QImage &image) const;
    void emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                 ImageItem *item, const QImage &preferredDisplay,
                                 bool hasCrop);


    // --- Crop host extras (controller needs without friend) ---
    void cancelZoomRegion();
    void scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                        const WorkspaceItemState &want);
    bool loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const;
    void restoreSessionCropAppearance(ImageItem *item);
    void updateMouseInfo(const QPoint &viewPos);
    Qt::AspectRatioMode currentFitAspectMode() const;

    // --- Slideshow host (Phase 6: replaces friend SlideshowController) ---
    SessionAppearanceStore &hostAppearance() { return appearance(); }
    const SessionAppearanceStore &hostAppearance() const { return appearance(); }
    ViewFraming &hostFraming() { return m_framing; }
    const ViewFraming &hostFraming() const { return m_framing; }
    CanvasBackground &hostCanvasBg() { return m_canvasBg; }
    const CanvasBackground &hostCanvasBg() const { return m_canvasBg; }
    SessionIdentity &hostSessionId() { return m_sessionId; }
    const SessionIdentity &hostSessionId() const { return m_sessionId; }
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
        return m_pathOrderBook.firstIdForPath(path);
    }
    PathItemStateBook &hostItemStateBook() { return m_itemStateBook; }
    const PathItemStateBook &hostItemStateBook() const { return m_itemStateBook; }
    HudAppearance &hostHudPrefs() { return m_hudPrefs; }
    const HudAppearance &hostHudPrefs() const { return m_hudPrefs; }
    HudFlash &hostHudFlash() { return m_hudFlash; }
    const HudFlash &hostHudFlash() const { return m_hudFlash; }
    QTimer *&hostHudFlashTimer() { return m_hudFlashTimer; }
    QTimer *hostHudFlashTimer() const { return m_hudFlashTimer; }
    DisplaySurfaceController &hostDisplaySurfaces() { return m_displayPipeline.displaySurfaces(); }
    const DisplaySurfaceController &hostDisplaySurfaces() const { return m_displayPipeline.displaySurfaces(); }
    TileNeighborPrefetch &hostTileNeighborPrefetch() { return m_tileNeighborPrefetch; }
    const TileNeighborPrefetch &hostTileNeighborPrefetch() const { return m_tileNeighborPrefetch; }
    EdgeZone hostHoverEdge() const { return m_hoverEdge; }
    EdgeZone edgeZoneAt(const QPoint &viewPos) const;

    // --- Display/crop host (controller access without friend) ---
    ImageItem *primaryItem() const;
    void applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want);
    void onPoolCropFullRasterDecoded(const QString &path, const QImage &decoded, quint64 gen);
    void restoreAttentionPoints(const QVector<QPointF> &pts);
    void attachDisplaySample(ImageItem *item, const QImage &display,
                             const WorkspaceItemState &want,
                             SessionAppearance::PixelKind kind);
    void setHostHoverEdge(EdgeZone z) { m_hoverEdge = z; }
    SessionLoadGate &hostLoadGate() { return m_displayPipeline.loadGate(); }
    const SessionLoadGate &hostLoadGate() const { return m_displayPipeline.loadGate(); }
    /** PreferCache / soft climb edge cap (also used outside slideshow). */
    int cappedDisplayEdgeForPath(const QString &path, int wantEdge) const;
    QSize ensureSlideshowLogicalSize(const QString &path);
    bool isProvisionalImageSize(const QString &path) const;
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    QString currentPath() const;

