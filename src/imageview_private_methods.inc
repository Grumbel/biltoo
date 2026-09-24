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

    static EdgeZone edgeZoneFromPolicy(EdgeNavPolicy::Zone z)
    {
        switch (z) {
        case EdgeNavPolicy::Zone::Previous: return EdgeZone::Previous;
        case EdgeNavPolicy::Zone::Next: return EdgeZone::Next;
        case EdgeNavPolicy::Zone::GalleryReturn: return EdgeZone::GalleryReturn;
        case EdgeNavPolicy::Zone::None:
        default: return EdgeZone::None;
        }
    }

    static EdgeNavPolicy::Zone edgeZoneToPolicy(EdgeZone z)
    {
        switch (z) {
        case EdgeZone::Previous: return EdgeNavPolicy::Zone::Previous;
        case EdgeZone::Next: return EdgeNavPolicy::Zone::Next;
        case EdgeZone::GalleryReturn: return EdgeNavPolicy::Zone::GalleryReturn;
        case EdgeZone::None:
        default: return EdgeNavPolicy::Zone::None;
        }
    }

    /** qCritical if two live/stashed items share one SessionImageId. */
    bool validateUniqueLiveSessionIds(const char *context = nullptr) const;

    /**
     * Pack-order append (Explicit; seed from doc when promoting out of FollowDocument).
     * Place/addImageForSession only — not a controller host API.
     */
    void pathOrderAppendRow(const QString &path, SessionImageId id = kInvalidSessionImageId)
    {
        m_session.pathOrder().appendExplicitRow(path, id, m_sessionDoc);
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

    // --- completeLoadReplace phases ---

    /** Scene pixels per millimetre for the page guide (layout scale). */

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

    /** After slider idle: durable save, filmstrip, full rematerialize if needed. */


    SessionImageId resolveContentEditSessionId(const ImageItem *item) const;


    void appendThumtooDebugStatus(QString *text, ImageItem *item) const;

    QString statusTextEmpty() const;

    QString statusTextMultiItem(ImageItem *item, const QString &quality,
                                int edge, const QSize &native) const;


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
    void scheduleColorAdjustCommit(SessionImageId sid, const QString &path);
    void paintTextRubberBandOverlay(QPainter &painter);
    /** Gallery multi-select frames in scene space (not inside item paint cache). */
    void paintHudPanels(QPainter &painter);
    /**
     * Map a page text/link region into the primary item's current image-pixel
     * space (after session content flip / quarter-turn bake). Empty if the
     * layer or item is unavailable.
     */
    /** Map viewport rubber rect → image-pixel rect on primary item. */
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
    bool tryMousePressImageLink(QMouseEvent *event);
    bool tryMousePressTextRubber(QMouseEvent *event);

    // mouseMoveEvent phases
    bool tryMouseMoveTextRubber(QMouseEvent *event);
    bool tryMouseMoveZoomRegion(QMouseEvent *event);
    bool tryMouseMovePageGuide(QMouseEvent *event);
    bool tryMouseMoveGroupAndHandleDrag(QMouseEvent *event);

    // mouseReleaseEvent phases
    void pushItemTransformUndo(ImageItem *item, const ItemComponents::Placement &before,
                               const ItemComponents::Placement &after, const QString &text);
    bool tryMouseReleaseTextRubber(QMouseEvent *event);
    bool tryMouseReleaseZoomRegion(QMouseEvent *event);
    bool tryMouseReleasePageGuide(QMouseEvent *event);
    bool tryMouseReleaseGroupDrag(QMouseEvent *event);
    bool tryMouseReleaseHandleDrag(QMouseEvent *event);
    bool tryMouseReleaseItemDrag(QMouseEvent *event);

    // keyPressEvent phases
    bool tryKeyPressZoomRegion(QKeyEvent *event);

    // wheelEvent phases
