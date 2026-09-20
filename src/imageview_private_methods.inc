// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Private method declarations for ImageView (Phase 6 Tier 0).
// Included only from imageview.h inside the class `private:` section.
// Do not include from other headers. Methods have no references outside imageview*.

    static bool isNavEdge(EdgeZone zone)
    {
        return zone == EdgeZone::Previous || zone == EdgeZone::Next
            || zone == EdgeZone::GalleryReturn;
    }

    bool hasHoverNavEdge() const { return isNavEdge(m_hoverEdge); }

    /** Add image and place its centre at scenePos once loaded. */
    bool addImageAt(const QString &path, const QPointF &scenePos);

    /** Session slots currently represented on the Workspace canvas. */
    QSet<int> workspaceSessionIndices() const;

    /**
     * Assign sessionIndex from @p sessionFiles order (legacy). Prefer
     * rebindWorkspaceSession with stable ids.
     * Matches by path occurrence so duplicates and older unbound items work.
     */
    void rebindWorkspaceSessionIndices(const QStringList &sessionFiles);

    /** qCritical if two live/stashed items share one SessionImageId. */
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;

    bool hasCentreProgress() const { return m_centreProgress.active(); }

    /** Controller host: if live items exist and none selected, select first. */
    void ensurePrimarySelection();














    /** @deprecated name — use logicalSizeForPath. */
    QSize slideshowLogicalSize(const QString &path) const {
        return logicalSizeForPath(path);
    }

    /**
     * Ensure logical size is known (may schedule async probe).
     * Phase entry, fit, and framing call this before geometry.
     */
    QSize ensureLogicalSizeForPath(const QString &path);

    /** Static centre-crop cover of @p image to the current viewport size. */
    QPixmap renderCoverPixmap(const QImage &image) const;

    /** Dedicated HUD line: Loading · N active · cache vs file/archive. */
    QString loadingStatusHudLine() const;





    /** How many times @a path appears in session path order (duplicate tiles). */

    /**
     * View-local path order (Gallery pack / LoadAdd multiplicity).
     * All reads go through these accessors so Tier 4 can swap the backing store.
     * SessionDocument remains MainWindow-owned — mutations never clear/append it
     * (Gallery leave clears the book without wiping the session).
     */
    void pathOrderClear()
    {
        m_pathOrderBook.clear();
    }
    void pathOrderSetOrder(const QStringList &paths, const QVector<SessionImageId> &ids)
    {
        m_pathOrderBook.setOrder(paths, ids);
    }
    void pathOrderAppendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_pathOrderBook.appendRow(path, id);
    }

    /** Re-apply scrollbar policies so AsNeeded ranges update after fit/zoom. */
    void refreshScrollBarGeometry();

    /** Claim one pending restore snapshot for @a path (FIFO; duplicates OK). */

    /**
     * LoadReplace: Image-mode navigation install, or empty multi-item seed.
     */

    /** LoadRestore: create tile from decode + claimed restore snapshot. */

    /**
     * LoadAdd: fill/create tiles for @a path after a successful or failed decode.
     * Honours load generation and pending-workspace cancellation.
     */










    // --- completeLoadReplace phases ---


    bool pageGuideSelected() const { return m_pageGuide.isSelected(); }


    /** Scene pixels per millimetre for the page guide (layout scale). */
    static qreal pageGuidePxPerMm();


    ViewMode viewMode() const { return m_viewMode; }


    bool galleryReturnAvailable() const { return m_sessionNav.isGalleryReturnAvailable(); }

    bool imageModeNavigationEnabled() const { return m_sessionNav.isImageModeNavEnabled(); }

    /**
     * Same as path-only overload, but binds each path to the parallel
     * @p sessionIds entry (stable SessionImageId). Gallery/Workspace loads
     * then apply per-id crop/flip/rotate from appearance().
     */
    QList<ImageItem *> collectDoomedWorkspaceItems(const QStringList &paths,
                                                   const QVector<SessionImageId> &sessionIds) const;

    void destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed);

    void finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                 const QVector<SessionImageId> &sessionIds);


    bool galleryRelayoutSuppressed() const { return m_galleryRelayoutSuppress.active(); }

    /** Remove one image from the workspace, remembering its transform. */
    void removeWorkspacePath(const QString &path);

    /** Remove the canvas item bound to this session slot (duplicate-safe). */
    void removeWorkspaceSessionIndex(int sessionIndex);

    bool hasWorkspaceSessionIndex(int sessionIndex) const;

    ImageItem *findItemBySessionIndex(int sessionIndex) const;

    QList<ImageItem *> collectItemsForSessionId(SessionImageId sessionId) const;

    QStringList destroySessionIdItems(const QList<ImageItem *> &doomed);

    void prunePendingBindsAndSavedForSessionId(SessionImageId sessionId);

    void prunePathOrdersAfterSessionRemove(const QStringList &removedPaths);

    void restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                           const QPointF &keptCenter, int scrollH, int scrollV);

    /** Hide canvas tile(s) for @p sessionId without dropping session appearance. */
    void detachCanvasSessionId(SessionImageId sessionId);

    /** Remove the n-th canvas item with @p path (0-based, m_items order). */
    void removeWorkspacePathOccurrence(const QString &path, int occurrence);

    /** Image-mode framing after soft/full install (honours sticky zoom). */

    /** Best-effort: remember viewport centre in image-normalized coords. */

    void restoreStickyPanAnchor(ImageItem *item);

    bool zoomRegionArmed() const { return m_zoomRegion.isArmed(); }

    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;



    /** Undo/redo: replace attention points on the current attention target. */



    bool showTextRegions() const { return m_textLayer.showsRegions(); }


    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;


    void clearViewBackground(); /**< AppDefault */

    int sessionTotal() const { return m_sessionId.currentTotal(); }

    bool slideshowPausedHud() const { return m_slideshow.hud().isPausedHud(); }






















    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    /** Workspace: remove selected tiles from the canvas only (session kept). */
    void removeSelectedCanvasItems();


    /**
     * Coalesce rapid Gallery packs (e.g. sizeReady storm when opening a large
     * archive). Runs applyLayout after a short idle; Enter/Explicit callers
     * should still use applyLayout directly.
     */

    void persistSessionAppearanceSlot(ImageItem *item);

    void syncSessionEditPeers(ImageItem *item);

    void updateWorkspaceSavedAppearance(ImageItem *item);

    /**
     * After content appearance changed for @p item: filmstrip id override,
     * Gallery pack, Workspace sceneRect, or Image mode sceneRect.
     * Called from commitItemSessionEdit — keep mode widgets in sync.
     */
    void propagateSessionAppearanceToViews(ImageItem *item);

    QImage sessionAppearanceImage(const ImageItem *item) const;

    /**
     * Bake stored content flips/rotates (and grade) onto a decode of @p path
     * for session @p sid. Used for soft placeholders and slideshow cache blits.
     * Crop is skipped without full-source geometry (flip/rotate only).
     */
    QImage imageWithSessionAppearance(const QImage &src, SessionImageId sid,
                                      const QString &path = QString()) const;

    ColorAdjustments targetColorAdjustments() const;

    /** Interactive grade: clamped host bake (no SQLite / filmstrip). */
    void applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want);

    /** After slider idle: durable save, filmstrip, full rematerialize if needed. */
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);

    void flushColorAdjustCommit();

    /** Bake ±90° content into pixels and session state (not placement). */
    WorkspaceItemState captureContentBakeBeforeState(ImageItem *item) const;

    SessionImageId resolveContentEditSessionId(ImageItem *item) const;

    WorkspaceItemState appearanceCropMapForEdit(ImageItem *item,
                                                const WorkspaceItemState &fallback,
                                                SessionImageId sid) const;

    void persistDurableContentAppearance(ImageItem *item, const WorkspaceItemState &s,
                                         const char *debugTag);

    void bakeItemRotate90(ImageItem *item, int quarterTurns);

    /** Push geometry-only undo (pos/scale/rotation/opacity/z/item flips). */

    /** Push content bake undo (pixels + session appearance). */
    void pushItemContentCommand(const QString &text, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &before,
                                const WorkspaceItemState &after);

    void appendThumtooDebugStatus(QString *text, ImageItem *item) const;

    QString statusTextEmpty() const;

    QString statusTextMultiItem(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;

    QString imageModeClimbActivityLabel(const ImageItem *item) const;

    QString statusTextImageMode(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;

    /** User-facing quality of pixels currently shown for @p item. */
    QString pixelQualityLabel(const ImageItem *item) const;


    /** Basename of the current/target image for the bottom HUD. */
    QString hudFileName() const;

    ImageMouseInfo mouseInfo() const { return m_chrome.currentMouseInfo(); }

    /** Clear canvas selection. */
    void clearCanvasSelection();



    /** Coalesce statusChanged during soft climb (MainWindow is not free). */


