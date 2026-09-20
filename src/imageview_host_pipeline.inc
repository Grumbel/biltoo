// Display pipeline host methods (Tier 5c; was private friend surface).
// Included from ImageView public section (after public slots).

    // --- Display pipeline host methods (Tier 5c; was private friend surface) ---
    void clearTextSelection();
    void refreshTextLayer();
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);
    void syncImageModeSceneRect(ImageItem *item);
    void captureStickyPanAnchor(ImageItem *item);
    void applyImageModeFraming(ImageItem *item);
    int pathOrderOccurrences(const QString &path) const;
    /** Drop neighbor-prefetch slot for one path (session remove / tile purge). */
    void dropTilePrefetchPath(const QString &path);
    bool pathOrderIsEmpty() const { return currentPackOrder().isEmpty(); }
    /** Pack source snapshot (Tier 4: currently fromBook; document when aligned). */
    PackOrderView currentPackOrder() const { return PackOrderView::fromBook(m_pathOrderBook); }
    QSize layoutSizeForPath(const QString &path, const QImage &previewHint = QImage());
    void scheduleImageSizeProbe(const QString &path);
    void preserveImageViewOnLogicalSizeChange(ImageItem *item, const QSize &before,
                                              const QSize &after);
    bool takePendingSessionBind(const QString &path, PendingSessionBind *out);
    void placeNewLoadAddItem(ImageItem *item, const QString &path, const QImage &image,
                             bool haveBound, const PendingSessionBind &bound);
    void purgeSatisfiedPendingBinds(const QString &path);
    int countPendingSessionBinds(const QString &path) const;
    bool takePendingSessionBindForNewItem(const QString &path, ImageItem *item,
                                          PendingSessionBind *out);
    bool installFullPreservingWorkspaceFootprint(ImageItem *item, const QImage &image);
    void applyStoredAppearance(ImageItem *item);
    void applyPendingBindScenePos(ImageItem *item, const PendingSessionBind &bound);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void paintViewportOverlays(QPainter &painter);
    void paintTextRubberBandOverlay(QPainter &painter);
    void paintWorkspaceViewportChrome(QPainter &painter);
    /** Gallery multi-select frames in scene space (not inside item paint cache). */
    void paintGallerySelectionFrames(QPainter *painter, const QRectF &exposed) const;
    void paintSlideshowLetterboxComposite(QPainter &painter);
    void paintEmptySessionInvite(QPainter &painter);
    void paintHudPanels(QPainter &painter);
    void paintSlideshowSeekbar(QPainter &painter);
    void recomputeTextSearchMatches();
    void finishTextRubberBand();
    [[nodiscard]] bool pageYUpForTextLayer() const;
    /**
     * Map a page text/link region into the primary item's current image-pixel
     * space (after session content flip / quarter-turn bake). Empty if the
     * layer or item is unavailable.
     */
    [[nodiscard]] QRectF textRegionImageRect(const ThumtooCache::TextRegion &region) const;
    /** Map viewport rubber rect → image-pixel rect on primary item. */
    [[nodiscard]] QRectF textRubberBandImageRect() const;
    /** Hit-test link region under view pos; sets page/uri outs. */
    [[nodiscard]] bool hitTextLinkAt(const QPoint &viewPos, int *pageOut,
                                    QString *uriOut) const;

    void drawBackground(QPainter *painter, const QRectF &rect) override;
    /** Scene-space canvas background (AppDefault or Workspace override). */
    void paintCanvasBackground(QPainter *painter, const QRectF &rect, qreal viewScale);
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    // mousePressEvent phases (return true if the event was handled)
    bool tryMousePressZoomRegion(QMouseEvent *event);
    bool tryMousePressWorkspaceChrome(QMouseEvent *event);
    bool tryMousePressImageLink(QMouseEvent *event);
    bool tryMousePressTextRubber(QMouseEvent *event);
    bool tryMousePressPan(QMouseEvent *event);
    bool tryMousePressWorkspaceRotate(QMouseEvent *event);

    // mouseMoveEvent phases
    bool tryMouseMoveTextRubber(QMouseEvent *event);
    void updateMouseMoveLinkHover(QMouseEvent *event);
    bool tryMouseMovePan(QMouseEvent *event);
    bool tryMouseMoveZoomRegion(QMouseEvent *event);
    bool tryMouseMovePageGuide(QMouseEvent *event);
    bool tryMouseMoveGroupAndHandleDrag(QMouseEvent *event);
    bool tryMouseMoveWorkspaceRotate(QMouseEvent *event);
    void updateMouseMoveWorkspaceChromeHover(QMouseEvent *event);

    // mouseReleaseEvent phases
    void restoreToolCursor();
    void pushItemTransformUndo(ImageItem *item, const WorkspaceItemState &before,
                               const WorkspaceItemState &after, const QString &text);
    bool tryMouseReleaseTextRubber(QMouseEvent *event);
    bool tryMouseReleaseZoomRegion(QMouseEvent *event);
    bool tryMouseReleasePageGuide(QMouseEvent *event);
    bool tryMouseReleaseGroupDrag(QMouseEvent *event);
    bool tryMouseReleaseHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseWorkspaceRotate(QMouseEvent *event);
    bool tryMouseReleasePan(QMouseEvent *event);
    bool tryMouseReleaseItemDrag(QMouseEvent *event);

    // keyPressEvent phases
    bool tryKeyPressZoomRegion(QKeyEvent *event);
    bool tryKeyPressSelectAll(QKeyEvent *event);

    // wheelEvent phases
    void wheelZoomViewAboutCursor(QWheelEvent *event);
