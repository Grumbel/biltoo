// Crop / display host operations (Phase 6 Tier 2b / 5c).
// Bag accessors live in imageview_host_accessors.inc.
// Included from ImageView public section.

    // --- Crop host (Phase 6 Tier 2b CropController) ---
    void storeCropAppearance(ImageItem *item, SessionImageId sid,
                             const WorkspaceItemState &s);
    bool loadRestoreCropAppearance(ImageItem *item, WorkspaceItemState *app,
                                    SessionImageId *sidOut) const;
    void rememberSizeFromDecode(const QString &path, const QImage &image) override;
    void rememberImageSize(const QString &path, const QSize &size);
    void emitCropApplyAppearance(SessionImageId sid, const QString &path,
                                 ImageItem *item, const QImage &preferredDisplay,
                                 bool hasCrop);

    // --- Crop host extras (controller needs without friend) ---
    void cancelZoomRegion();
    bool loadSessionAppearance(SessionImageId sid, WorkspaceItemState *st) const;
    void restoreSessionCropAppearance(ImageItem *item);
    void updateMouseInfo(const QPoint &viewPos);
    Qt::AspectRatioMode currentFitAspectMode() const override;

    // --- Display / framing host ops ---
    EdgeZone edgeZoneAt(const QPoint &viewPos) const;
    ImageItem *primaryItem() const override;
    void setHostHoverEdge(EdgeZone z) { m_hoverEdge = z; }
    /** Resolve/cache logical pixel size for @p path (probes if needed). */
    QSize ensureLogicalSizeForPath(const QString &path);
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio) override;
    QString currentPath() const;
