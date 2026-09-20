// Display pipeline host methods (Tier 5c; was private friend surface).
// Included from ImageView public section (after public slots).

    // --- Display pipeline host methods (Tier 5c; was private friend surface) ---
    void clearTextSelection();
    void refreshTextLayer();
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);
    void syncImageModeSceneRect(ImageItem *item);
    void captureStickyPanAnchor(ImageItem *item);
    void applyImageModeFraming(ImageItem *item);
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
     * append → Explicit; seed from doc when promoting out of FollowDocument.
     */
    void pathOrderClear() { m_pathOrderOverlay.clearExplicit(); }
    void pathOrderSetOrder(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        m_pathOrderOverlay.setExplicit(paths, ids);
        m_pathOrderOverlay.tryCollapseToFollowDocument(m_sessionDoc);
    }
    void pathOrderAppendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_pathOrderOverlay.appendExplicitRow(path, id, m_sessionDoc);
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
