// Display pipeline host methods (Tier 5c; was private friend surface).
// Included from ImageView public section (after public slots).

    // --- Display pipeline host methods (Tier 5c; was private friend surface) ---
    void clearTextSelection();
    void refreshTextLayer();
    bool isCropDraftLockedPath(const QString &path) const;
    void maybeUpgradeCropFullRaster(const QString &path, const QImage &image);
    void scheduleGalleryStatusRefresh(int delayMs = 100);
    void syncImageFocusSurfaceState();
    void seedSessionAppearanceFromState(SessionImageId sid, const QString &path);
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);
    void syncImageModeSceneRect(ImageItem *item);
    void frameImageModeReplaceItem(ImageItem *item, const QString &path);
    void seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image);
    int galleryHaveEdgeFromItems(const QString &path, bool *anyFullOut = nullptr) const;
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
    WorkspaceItemState wantAppearanceForItem(const ImageItem *item,
                                             SessionImageId sid = kInvalidSessionImageId) const;
    void bindImageModeSessionCursor(ImageItem *item);
    void resetImageModeItemPlacement(ImageItem *item);
    void driveImageFocusSurface();
    ImageItem *createPlaceholderItem(const QString &path, const QSize &intrinsicSize);
    ImageItem *imageModeItemForPath(const QString &path) const;
    void gallerySoftResetPath(const QString &path);
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
    ImageItem *createItemFromImage(const QString &path, const QImage &image,
                                   bool applyStoredSessionCrop = true);

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
    bool tryMousePressSlideshowSeek(QMouseEvent *event);
    bool tryMousePressAttention(QMouseEvent *event);
    bool tryMousePressCrop(QMouseEvent *event);
    bool tryMousePressZoomRegion(QMouseEvent *event);
    bool tryMousePressWorkspaceChrome(QMouseEvent *event);
    bool tryMousePressImageLink(QMouseEvent *event);
    bool tryMousePressTextRubber(QMouseEvent *event);
    bool tryMousePressImageEdges(QMouseEvent *event);
    bool tryMousePressPan(QMouseEvent *event);
    bool tryMousePressWorkspaceRotate(QMouseEvent *event);
    bool tryMousePressGalleryRight(QMouseEvent *event);
    bool tryMousePressGalleryLeft(QMouseEvent *event);
    bool tryMousePressWorkspaceSelect(QMouseEvent *event);

    // mouseMoveEvent phases
    bool tryMouseMoveTextRubber(QMouseEvent *event);
    void updateMouseMoveLinkHover(QMouseEvent *event);
    bool tryMouseMoveAttention(QMouseEvent *event);
    bool tryMouseMoveCropDrag(QMouseEvent *event);
    bool tryMouseMovePan(QMouseEvent *event);
    bool tryMouseMoveCropHover(QMouseEvent *event);
    bool tryMouseMoveZoomRegion(QMouseEvent *event);
    bool tryMouseMovePageGuide(QMouseEvent *event);
    bool tryMouseMoveGroupAndHandleDrag(QMouseEvent *event);
    bool tryMouseMoveWorkspaceRotate(QMouseEvent *event);
    void updateMouseMoveSlideshowSeek(QMouseEvent *event);
    void updateMouseMoveWorkspaceChromeHover(QMouseEvent *event);

    // mouseReleaseEvent phases
    void restoreToolCursor();
    void pushItemTransformUndo(ImageItem *item, const WorkspaceItemState &before,
                               const WorkspaceItemState &after, const QString &text);
    bool tryMouseReleaseSlideshowSeek(QMouseEvent *event);
    bool tryMouseReleaseTextRubber(QMouseEvent *event);
    bool tryMouseReleaseAttention(QMouseEvent *event);
    bool tryMouseReleaseCrop(QMouseEvent *event);
    bool tryMouseReleaseZoomRegion(QMouseEvent *event);
    bool tryMouseReleasePageGuide(QMouseEvent *event);
    bool tryMouseReleaseGroupDrag(QMouseEvent *event);
    bool tryMouseReleaseHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseWorkspaceRotate(QMouseEvent *event);
    bool tryMouseReleasePan(QMouseEvent *event);
    bool tryMouseReleaseItemDrag(QMouseEvent *event);

    // keyPressEvent phases
    bool tryKeyPressAttention(QKeyEvent *event);
    bool tryKeyPressCrop(QKeyEvent *event);
    bool tryKeyPressZoomRegion(QKeyEvent *event);
    bool tryKeyPressSelectAll(QKeyEvent *event);
    bool tryKeyPressImageNavigate(QKeyEvent *event);
    bool tryKeyPressGallery(QKeyEvent *event);
    bool tryKeyPressWorkspaceShear(QKeyEvent *event);
    bool tryKeyPressDeleteSelection(QKeyEvent *event);

    // wheelEvent phases
    bool tryWheelGalleryZoom(QWheelEvent *event);
    bool tryWheelGalleryScroll(QWheelEvent *event);
    void wheelZoomViewAboutCursor(QWheelEvent *event);
