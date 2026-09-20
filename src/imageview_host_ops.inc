// Controller host operations (mode controllers; prefer over friend).
// Included from ImageView public section.

    // --- Controller host operations (mode controllers; prefer these over friend) ---
    /** Apply interactive/gallery/static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item);
    /** Gallery stash restore: bake session crop/orient onto the tile if needed. */
    void rematerializeGalleryItemFromStore(ImageItem *item);
    /**
     * Controller host: budgeted tile pump/issue for viewport items.
     * Gallery restore uses this so Image-mode path RAM paints without waiting
     * for the decode-window timer.
     */
    void tickPrimaryTileLod(int budget = 8);
    /** @deprecated Path is not identity; prefer findItemBySessionId. */
    ImageItem *findItemByPath(const QString &path) const;
    /**
     * Resolve a path to a live item without always taking the first match.
     * Prefer a selected item with that path, else the sole live match.
     * Returns nullptr when ambiguous (multiple unselected matches) or none.
     */
    ImageItem *findPreferredItemForPath(const QString &path) const;
    ImageItem *targetItem() const;
    void destroyCanvasItem(ImageItem *item);
    void updateWorkspaceSceneRect();
    /** Queue LoadRestore for workspace rebuild from durable snapshot. */
    void scheduleRestoreLoad(const QString &path);
    void snapshotWorkspace();
    void discardStashedWorkspace();
    /** Clear durable Workspace arrangement (session open ≠ project). */
    void clearDurableWorkspaceSnapshot();
    void snapshotFreeFormStates();

    /** Destroy live canvas items only; keep Workspace/Gallery stashes. */
    void clearLiveCanvas();

    /** Workspace: show a paper-sized frame (scene units) for print layout. */
    void setPageGuideVisible(bool on);
    bool pageGuideVisible() const { return m_pageGuide.isVisible(); }
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
    /** Drop cached Gallery tiles (e.g. after loading a new session). */
    void discardStashedGallery();

