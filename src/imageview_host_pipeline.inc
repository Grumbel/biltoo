// Display pipeline host methods (Tier 5c; was private friend surface).
// Included from ImageView public section (after signals).

    // --- Display pipeline host methods (Tier 5c; was private friend surface) ---
    /** True while @p gen is still the active LoadReplace generation (pool jobs). */
    void clearTextSelection();
    bool matchesLoadGeneration(quint64 gen) const
    {
        return m_displayPipeline.loadGate().accepts(gen);
    }
    void refreshTextLayer();
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);
    void syncImageModeSceneRect(ImageItem *item) override;
    void captureStickyPanAnchor(ImageItem *item) override;
    void restoreStickyPanAnchor(ImageItem *item);
    void applyImageModeFraming(ImageItem *item) override;
    /** View-local pack multiplicity (never SessionDocument — pathOrderClear). */
    int pathOrderOccurrences(const QString &path) const
    {
        return currentPackOrder().countPathOccurrences(path);
    }
    bool pathOrderIsEmpty() const { return currentPackOrder().isEmpty(); }
    /**
     * Pack source snapshot via PackOrderOverlay::resolve.
     * SessionDocument alone is not a pack drop-in (PATH_ORDER.md).
     */
    PackOrderView currentPackOrder() const
    {
        return m_pathOrderOverlay.resolve(m_sessionDoc);
    }
    /**
     * Pack-order overlay mutators (not SessionDocument).
     * clear → Explicit empty (never FollowDocument while doc still has rows).
     * setOrder → Explicit then optional collapse when aligned with bound doc.
     * append is private (pathOrderAppendRow) — only ImageView place/add paths.
     */
    void pathOrderClear() { m_pathOrderOverlay.clearExplicit(); }
    void pathOrderSetOrder(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        m_pathOrderOverlay.setExplicit(paths, ids);
        m_pathOrderOverlay.tryCollapseToFollowDocument(m_sessionDoc);
    }
    QSize layoutSizeForPath(const QString &path, const QImage &previewHint = QImage());
    void scheduleImageSizeProbe(const QString &path);
    void preserveImageViewOnLogicalSizeChange(ImageItem *item, const QSize &before,
                                              const QSize &after);
    void placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                             bool haveBound, const PendingSessionBind &bound);
    void purgeSatisfiedPendingBinds(const QString &path);
    bool takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                          PendingSessionBind *out);
    bool installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image);
    void applyStoredAppearance(ImageItem *item);
    void applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound);
