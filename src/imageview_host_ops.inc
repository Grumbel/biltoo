// Controller host operations (mode controllers; prefer over friend).
// Included from ImageView public section.

    // --- Controller host operations (mode controllers; prefer these over friend) ---
    /** Apply interactive/gallery/static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item);
    /** Gallery stash restore: bake session crop/orient onto the tile if needed. */
    void rematerializeGalleryItemFromStore(ImageItem *item);
    /** @deprecated Path is not identity; prefer findItemBySessionId. */
    ImageItem *findItemByPath(const QString &path) const;
    /** Live canvas only (not Workspace/Gallery stashes). Same SessionImageId may
     * exist as a stashed Workspace tile while Gallery needs its own live cell. */
    ImageItem *findItemBySessionId(SessionImageId sessionId) const;
    /**
     * Resolve a path to a live item without always taking the first match.
     * Prefer a selected item with that path, else the sole live match.
     * Returns nullptr when ambiguous (multiple unselected matches) or none.
     */
    ImageItem *findPreferredItemForPath(const QString &path) const;
    /**
     * Best-effort path lookup: preferred, then first-match.
     * Use when a caller must have some item for the path (legacy bridge).
     * Prefer findItemBySessionId / findPreferredItemForPath when identity matters
     * and nullptr-on-ambiguous is acceptable.
     */
    ImageItem *findItemForPath(const QString &path) const;
    /** Gallery: selected tile, else first live item (keyboard open / focus). */
    ImageItem *selectedOrFirstGalleryItem() const;
    /** Emit sessionImageFocused / sessionSlotFocused / galleryItemFocused for @p item. */
    void emitGalleryItemFocus(ImageItem *item);
    /**
     * Open @p item in Image mode: SessionImageId → list index → path.
     * Gallery: when unbound without list cache, live canvas index is a pack-aligned
     * slot hint (after reorderItemsByPaths). Path open remains last resort.
     */
    void emitItemOpenInImageMode(ImageItem *item);
    /** Page-guide selection chrome (Workspace print frame). */
    void setPageGuideSelected(bool on);
    ImageItem *targetItem() const;
    /**
     * Remove @p item from the scene and delete it.
     * @p persistState when true snapshots via rememberItemState (pose/content
     * for session ids that remain). Pass false after removeAppearance so
     * destroy cannot re-seed sparse appearance.
     */
    void destroyCanvasItem(ImageItem *item, bool persistState = true);
    void updateWorkspaceSceneRect();
    /** Queue LoadRestore for workspace rebuild from durable snapshot. */
    void scheduleRestoreLoad(const QString &path);

    /** Destroy live canvas items only; keep Workspace/Gallery stashes. */
    void clearLiveCanvas();

    /** Workspace: show a paper-sized frame (scene units) for print layout. */
    void setPageGuideVisible(bool on);
    /** Update guide size from a printer page layout (millimetres → scene px). */
    void setPageGuideFromPrinter(const QPrinter &printer);
    /** Size page guide to live content bounds (+ margin); shows the guide. */
    void fitPageGuideToContent(qreal marginPx = 16.0);
    /** Scene rectangle of the page guide (printer: centred; fit-content: content AABB). */
    QRectF pageGuideSceneRect() const;
    /** Paint current mode content into @p pageRect on @p painter (print/preview). */
    void renderForPrint(QPainter *painter, const QRectF &pageRect) const;
    /** Scene bounds of live image tiles (no chrome), with a small margin. */
    QRectF contentExportBounds() const;
    /** Rasterize @p sourceSceneRect into an image of @p pixelSize. */
    QImage renderExportImage(const QSize &pixelSize, const QRectF &sourceSceneRect,
                             bool transparentBackground = true) const;
    QList<ImageItem *> transformTargets() const;
    void pushItemGeometryCommand(const QString &text, ImageItem *item,
                                 const ItemComponents::Placement &before,
                                 const ItemComponents::Placement &after);

