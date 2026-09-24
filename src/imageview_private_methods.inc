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

    /** qCritical if two live/stashed items share one SessionImageId. */
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;

    /**
     * Pack-order append (Explicit; seed from doc when promoting out of FollowDocument).
     * Place/addImageForSession only — not a controller host API.
     */
    void pathOrderAppendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_pathOrderOverlay.appendExplicitRow(path, id, m_sessionDoc);
    }

    /** Stage 2: read live ImageItem pose into a Placement record. */
    static ItemComponents::Placement placementFromItem(const ImageItem *item);
    /**
     * Apply pose after geometry undo/redo so live item + ItemWorld Placement align.
     */
    void applyGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl);
    /** Write Placement (or path-book pose) without requiring a full DTO snapshot. */
    void persistGeometrySessionState(ImageItem *item, const ItemComponents::Placement &pl);

    /** Dedicated HUD line: Loading · N active · cache vs file/archive. */
    QString loadingStatusHudLine() const;

    /** Re-apply scrollbar policies so AsNeeded ranges update after fit/zoom. */
    void refreshScrollBarGeometry();

    // --- completeLoadReplace phases ---

    /** Scene pixels per millimetre for the page guide (layout scale). */
    static qreal pageGuidePxPerMm();

    /**
     * Choose live tiles to remove when setWorkspacePaths rebuilds membership.
     * Prefer SessionImageId; path occurrence counts only for unbound leftovers.
     */
    QList<ImageItem *> collectDoomedWorkspaceItems(const QStringList &paths,
                                                   const QVector<SessionImageId> &sessionIds) const;

    void destroyDoomedWorkspaceItems(const QList<ImageItem *> &doomed);

    void finishSetWorkspacePaths(bool haveIds, const QStringList &paths,
                                 const QVector<SessionImageId> &sessionIds);

    ImageItem *findItemBySessionIndex(int sessionIndex) const;

    QList<ImageItem *> collectItemsForSessionId(SessionImageId sessionId) const;

    QStringList destroySessionIdItems(const QList<ImageItem *> &doomed);

    void prunePendingBindsAndSavedForSessionId(SessionImageId sessionId);

    void prunePathOrdersAfterSessionRemove(const QStringList &removedPaths);

    void restoreViewportAfterSessionRemove(bool gallery, const QRectF &keptSceneRect,
                                           const QPointF &keptCenter, int scrollH, int scrollV);

    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;

    /** Joined text of the selection in reading order; empty if none. */
    QString selectedText() const;

    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

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

    /** Interactive grade: clamped host bake (no SQLite / filmstrip). */
    void applyInteractiveColorGrade(ImageItem *item, const WorkspaceItemState &want);

    /** After slider idle: durable save, filmstrip, full rematerialize if needed. */
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);

    void flushColorAdjustCommit();

    SessionImageId resolveContentEditSessionId(const ImageItem *item) const;
    void restoreStickyPanAnchor(ImageItem *item);
    bool sessionIdMatchesPath(SessionImageId id, const QString &path) const;
    void setItemIntrinsicSize(ImageItem *item, const QSize &size);
    void rematerializeGalleryItemFromStore(ImageItem *item);
    void rematerializeItemContent(ImageItem *item, const WorkspaceItemState &want);
    void clearStaleAppliedFingerprintIfNeeded(ImageItem *item);
    void clearItemDecodedPixels(ImageItem *item);
    void setItemPreviewImage(ImageItem *item, const QImage &preview);

    // Pipeline forwards (private — controllers use hostDisplayPipeline).
    void scheduleAsyncHostRematerialize(const QString &path, SessionImageId sid,
                                        const WorkspaceItemState &want);
    void applyContentLayoutSize(ImageItem *item, const WorkspaceItemState &want);
    void attachDisplaySample(ImageItem *item, const QImage &display,
                             const WorkspaceItemState &want,
                             SessionAppearance::PixelKind kind);
    void bakeItemRotate90(ImageItem *item, int quarterTurns);
    void bakeItemFlip(ImageItem *item, bool horizontal, bool vertical);

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

    // --- Paint / input phases (were public via host_pipeline mid-include) ---

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
    void pushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                               const ItemComponents::Placement &after, const QString &text);
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
