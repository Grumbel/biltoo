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

    /** @deprecated name — use ensureLogicalSizeForPath. */
    QSize ensureSlideshowLogicalSize(const QString &path) {
        return ensureLogicalSizeForPath(path);
    }


    bool isCropDraftLockedPath(const QString &path) const;

    /** Drop neighbor-prefetch slot for one path (session remove). */
    void dropTilePrefetchPath(const QString &path);

    /**
     * Drop tile sessions on every live item for @p path, purge registry RAM,
     * and cancel neighbor prefetch. Use on Reload / file-replaced.
     */
    void purgeTilePathRam(const QString &path);

    /** Drop tile sessions on live + stashed items (before session-wide invalidateAll). */
    void dropAllTileLodSessions();




    /** Static centre-crop cover of @p image to the current viewport size. */
    QPixmap renderCoverPixmap(const QImage &image) const;





    tilelod::TileLodController *slideshowTilesForPath(const QString &path) const;




















    /** Viewport × motion headroom → atlas budget (see SlideshowAtlasPolicy). */
    DwellAtlasParams dwellAtlasParams() const;






    /** Dedicated HUD line: Loading · N active · cache vs file/archive. */
    QString loadingStatusHudLine() const;





    /** How many times @a path appears in session path order (duplicate tiles). */
    int pathOrderOccurrences(const QString &path) const;

    const QVector<SessionImageId> &sessionIdOrder() const { return m_pathOrderBook.idList(); }

    /** Re-apply scrollbar policies so AsNeeded ranges update after fit/zoom. */
    void refreshScrollBarGeometry();

    /** Claim one pending restore snapshot for @a path (FIFO; duplicates OK). */
    bool takePendingRestoreState(const QString &path, WorkspaceItemState *out);

    /**
     * LoadReplace: Image-mode navigation install, or empty multi-item seed.
     */
    void completeLoadReplace(const QString &path, const QImage &image, quint64 generation);

    /** LoadRestore: create tile from decode + claimed restore snapshot. */
    void completeLoadRestore(const QString &path, const QImage &image);

    /**
     * LoadAdd: fill/create tiles for @a path after a successful or failed decode.
     * Honours load generation and pending-workspace cancellation.
     */
    void completeLoadAdd(const QString &path, const QImage &image, quint64 generation);

    // --- completeLoadAdd phases (private; keep the driver thin) ---
    void finishLoadAddStatus(bool refreshGalleryWindow);

    bool acceptPendingLoadAdd(const QString &path, quint64 generation);

    void handleLoadAddDecodeFailure(const QString &path);

    void fillStashedItemsForPath(const QString &path, const QImage &image);

    void reassertPendingBindPlacement(const QString &path);

    void claimUnboundItemsForPendingBinds(const QString &path, const QImage &image);

    int fillLiveItemsWithDecodedPixels(const QString &path, const QImage &image,
                                       bool *sizeChangedOut);

    void createMissingLoadAddItems(const QString &path, const QImage &image,
                                   int have, int wanted);

    void applyLoadAddLayoutAfterMembership(bool sizeChanged);

    // --- completeLoadReplace phases ---
    void installImageModeReplaceItem(const QString &path, const QImage &image);

    void seedEmptyWorkspaceFromReplace(const QString &path, const QImage &image);

    bool pageGuideSelected() const { return m_pageGuide.isSelected(); }

    void setPageGuideSelected(bool on);

    /** Scene pixels per millimetre for the page guide (layout scale). */
    static qreal pageGuidePxPerMm();

    /** @deprecated Prefer leaveForImageMode(); identical behaviour. */
    void leaveGalleryForImage() { leaveForImageMode(); }

    ViewMode viewMode() const { return m_viewMode; }

    bool isMultiItemMode() const { return m_viewMode != ViewMode::Image; }

    bool galleryReturnAvailable() const { return m_sessionNav.isGalleryReturnAvailable(); }

    bool imageModeNavigationEnabled() const { return m_sessionNav.isImageModeNavEnabled(); }

    /**
     * Same as path-only overload, but binds each path to the parallel
     * @p sessionIds entry (stable SessionImageId). Gallery/Workspace loads
     * then apply per-id crop/flip/rotate from m_appearance.
     */
    QList<ImageItem *> collectDoomedWorkspaceItems(const QStringList &paths,
                                                   const QVector<SessionImageId> &sessionIds) const;

    void destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed);

    void finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                 const QVector<SessionImageId> &sessionIds);

    /** Create Gallery placeholders for m_pathOrder using definitive sizes only. */
    void ensureGalleryPlaceholders();

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

    void captureStickyZoomFromCurrentFraming();

    /** Image-mode framing after soft/full install (honours sticky zoom). */
    void applyImageModeFraming(ImageItem *item);

    /** Best-effort: remember viewport centre in image-normalized coords. */
    void captureStickyPanAnchor(ImageItem *item);

    void restoreStickyPanAnchor(ImageItem *item);

    void cancelZoomRegion();

    bool zoomRegionArmed() const { return m_zoomRegion.isArmed(); }

    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;

    bool enterCropModeFromUi();

    /** Re-run saliency detect on the current image (replaces all points). */
    void detectAttentionPoint();

    /** Undo/redo: replace attention points on the current attention target. */
    void restoreAttentionPoints(const QVector<QPointF> &pts);

    /** Shrink draft to non-background content (margin trim). */
    void requestCropViewportUpdate();

    void applyAutoCrop();

    bool showTextRegions() const { return m_textLayer.showsRegions(); }

    void refreshTextLayer();

    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;

    void clearTextSelection();

    void clearViewBackground(); /**< AppDefault */

    int sessionTotal() const { return m_sessionId.currentTotal(); }

    bool slideshowPausedHud() const { return m_slideshow.hud().isPausedHud(); }





















    /** SessionImageId for a session path (path order), or invalid. */
    SessionImageId sessionIdForPath(const QString &path) const;


    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    /** Workspace: remove selected tiles from the canvas only (session kept). */
    void removeSelectedCanvasItems();

    GalleryLayout::Mode galleryLayoutModeFromViewMode() const;

    /**
     * Coalesce rapid Gallery packs (e.g. sizeReady storm when opening a large
     * archive). Runs applyLayout after a short idle; Enter/Explicit callers
     * should still use applyLayout directly.
     */
    void requestDebouncedGalleryPack(GalleryPackReason reason = GalleryPackReason::ContentChange);

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
    void pushItemGeometryCommand(const QString &text, ImageItem *item,
                                 const WorkspaceItemState &before,
                                 const WorkspaceItemState &after);

    /** Push content bake undo (pixels + session appearance). */
    void pushItemContentCommand(const QString &text, ImageItem *item,
                                const QImage &beforeSrc, const QImage &afterSrc,
                                const WorkspaceItemState &before,
                                const WorkspaceItemState &after);

    /**
     * installDisplayPixels + preserveImageViewOnLogicalSizeChange.
     * Image-mode soft→full / soft→soft in-place upgrades (no zoom jump).
     */
    void installDisplayPreservingView(ImageItem *item, const QImage &pixels,
                                      SessionAppearance::PixelKind kind,
                                      SessionImageId sid);

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

    QString currentPath() const;

    /** Clear canvas selection. */
    void clearCanvasSelection();

    /** No-op: Gallery does not use setInterest (tiles + LQIP only). */
    void publishGalleryInterest(const QStringList &interestNear,
                                const QStringList &interestRest);

    void scheduleIdleGalleryDecodes(const QStringList &rest);

    void updateGalleryDecodeWindow();

    /**
     * Decode-window pass 1: attach ImageCache soft onto blank tiles (budgeted).
     * @return number of installs; @p morePending if the budget was exhausted.
     */
    int galleryInstallHostSoftOntoBlanks(int maxInstalls, bool *morePending);

    /** Coalesce statusChanged during soft climb (MainWindow is not free). */
    void scheduleGalleryStatusRefresh(int delayMs = 100);


