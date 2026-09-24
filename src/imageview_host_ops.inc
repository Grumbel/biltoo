// Controller host operations (mode controllers; prefer over friend).
// Included from ImageView public section.

    // --- Controller host operations (mode controllers; prefer these over friend) ---
    /** Apply interactive/gallery/static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item) override;
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
    /** Page-guide selection chrome (Workspace print frame). */
    void setPageGuideSelected(bool on);
    ImageItem *targetItem() const override;
    void hostPushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                                  const ItemComponents::Placement &after, const QString &text);
    void restoreToolCursor();
    /**
     * Remove @p item from the scene and delete it.
     * @p persistState when true snapshots via rememberItemState (pose/content
     * for session ids that remain). Pass false after removeAppearance so
     * destroy cannot re-seed sparse appearance.
     */
    void destroyCanvasItem(ImageItem *item, bool persistState = true);
    void updateWorkspaceSceneRect() override;

    /** Destroy live canvas items only; keep Workspace/Gallery stashes. */
    void clearLiveCanvas() override;

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
    /** High-res materialize of live tiles into @p targetRect (export/print). */
    void paintHighResExportItems(QPainter *painter, const QRectF &sourceScene,
                                 const QRectF &targetRect) const;
    /** Rasterize @p sourceSceneRect into an image of @p pixelSize. */
    QImage renderExportImage(const QSize &pixelSize, const QRectF &sourceSceneRect,
                             bool transparentBackground = true) const;
    QList<ImageItem *> transformTargets() const;
    void pushItemGeometryCommand(const QString &text, ImageItem *item,
                                 const ItemComponents::Placement &before,
                                 const ItemComponents::Placement &after);

    // --- Content bake host (DisplayPipelineController) ---
    WorkspaceItemState captureContentBakeBeforeState(ImageItem *item) const override;
    WorkspaceItemState appearanceCropMapForEdit(ImageItem *item,
                                                const WorkspaceItemState &fallback,
                                                SessionImageId sid) const override;
    void persistDurableContentAppearance(ImageItem *item, const WorkspaceItemState &s,
                                         const char *debugTag) override;
    void pushItemContentCommand(const QString &text, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &before,
                                const WorkspaceItemState &after) override;

    void refreshScrollBarGeometry();

    /** Controller host: filmstrip/session appearance sample for a live item. */
    QImage hostSessionAppearanceImage(const ImageItem *item) const
    {
        return sessionAppearanceImage(item);
    }

    /** Controller host: assert live + stash SessionImageId uniqueness. */
    bool hostValidateUniqueLiveSessionIds(const char *context = nullptr) const
    {
        return validateUniqueLiveSessionIds(context);
    }

    /** Controller host: live tile bound to session list index. */
    ImageItem *hostFindItemBySessionIndex(int sessionIndex) const
    {
        return findItemBySessionIndex(sessionIndex);
    }

    /** Controller host: snapshot bound appearance/pose before tile teardown. */
    void hostRememberItemState(ImageItem *item) { rememberItemState(item); }
    void hostPersistGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl)
    {
        persistGeometrySessionState(item, pl);
    }
    bool hostPreserveUndoOnDestroy() const { return m_preserveUndoOnDestroy; }

    /** Controller host: cache-only sizes + LQIP before first gallery pack. */
    void hostPrimeGalleryGeometryFromCache(const QStringList &paths)
    {
        primeGalleryGeometryFromCache(paths);
    }

    SessionImageId hostResolveContentEditSessionId(const ImageItem *item) const
    {
        return resolveContentEditSessionId(item);
    }

