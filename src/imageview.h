// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include <QGraphicsView>
#include <atomic>
#include <QHash>
class QTimer;
#include <QImage>
#include <QSet>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QUrl>

class ImageItem;
class QGraphicsScene;
class QUndoStack;

struct ImageMouseInfo {
    bool valid = false;
    QPoint imagePos;
    QColor pixelColor;
    QString path;
};

struct WorkspaceItemState {
    QString path;
    QPointF pos;
    qreal scale = 1.0;
    qreal rotation = 0.0;
    qreal opacity = 1.0;
    qreal z = 0.0;
    bool hFlip = false;
    bool vFlip = false;
};

/**
 * Central image area with three presentation modes:
 *
 * - Image: single centred non-interactive image; edge nav; slideshow.
 * - Gallery: session arranged by a packaged layout (masonry, grid, …);
 *   items are not freely moved; click opens Image mode for that file.
 * - Workspace: free-form multi-image canvas (move/scale/rotate/opacity/z).
 *
 * Gallery is not a Workspace layout — it is a separate mode of this view.
 */
class ImageView : public QGraphicsView
{
    Q_OBJECT

public:
    enum class Tool {
        Select,
        Pan
    };

    enum class ViewMode {
        Image,
        Gallery,
        Workspace
    };

    /** Hover / hit zone on the left or right edge in Image mode. */
    enum class EdgeZone {
        None,
        Previous,
        Next
    };

    explicit ImageView(QWidget *parent = nullptr);
    ~ImageView() override;

    bool loadImage(const QString &path);
    bool addImage(const QString &path);
    /** Add image and place its centre at scenePos once loaded. */
    bool addImageAt(const QString &path, const QPointF &scenePos);
    void clearWorkspace();

    void setViewMode(ViewMode mode);
    /** Reset view/scene so Image mode is not affected by prior canvas state. */
    void prepareImageModeCanvas();
    ViewMode viewMode() const { return m_viewMode; }
    bool isImageMode() const { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const { return m_viewMode == ViewMode::Workspace; }
    bool isMultiItemMode() const { return m_viewMode != ViewMode::Image; }

    /** @deprecated Prefer setViewMode(Workspace|Image). */
    void setWorkspaceMode(bool on);
    /** True only in free-form Workspace (not Gallery). */
    bool workspaceMode() const { return isWorkspaceMode(); }

    void clearExtras();

    /**
     * Enable left/right edge navigation affordances in Image mode.
     * Typically true when the session has more than one image.
     */
    void setImageModeNavigationEnabled(bool on);
    bool imageModeNavigationEnabled() const { return m_imageModeNavEnabled; }

    /**
     * Show exactly the given paths on the workspace. Images already present
     * keep their live transform; newly added ones restore saved state or get
     * a default placement. Images no longer listed are removed from the
     * scene after their state is saved.
     */
    void setWorkspacePaths(const QStringList &paths);
    /** Remove one image from the workspace, remembering its transform. */
    void removeWorkspacePath(const QString &path);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }

    QUndoStack *undoStack() const { return m_undoStack; }

    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    /** Cover the viewport (may crop); uses KeepAspectRatioByExpanding. */
    void zoomFill();
    /** Current view-level scale factor (workspace zoom). */
    qreal viewScale() const;
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();

    /** When true (default), left-drag pans in Image mode. */
    void setImageModeLeftDragPan(bool on);
    bool imageModeLeftDragPan() const { return m_imageModeLeftDragPan; }

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    bool hudVisible() const { return m_hudVisible; }

    /**
     * Brief VCR-style HUD message (action + optional detail).
     * Visible for ~1.8s; always shown even when the HUD is not pinned.
     */
    void flashHud(const QString &action, const QString &detail = QString());

    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    void raiseSelected();
    void lowerSelected();
    void opacityUp();
    void opacityDown();
    void opacityReset();

    /**
     * Arrangement of items. FreeForm is used only in Workspace mode.
     * Other values are Gallery layouts.
     */
    enum class LayoutMode {
        FreeForm,
        SideBySide,
        Vertical,
        Grid,
        Stack,
        /** Column packing (Pinterest-style): fixed column width, variable height. */
        Masonry
    };

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }
    void applyLayout();
    /** Coalesce resize-driven gallery relayouts. */
    void scheduleApplyLayout();

    /** Gallery mode with a packaged layout. */
    bool isGalleryLayout() const { return isGalleryMode(); }

    /** Enter Gallery mode and apply the given packaged layout (not FreeForm). */
    void enterGallery(LayoutMode packagedLayout);

    /** Fixed column width in pixels for Masonry (images scale to this width). */
    void setMasonryColumnWidth(int pixels);
    int masonryColumnWidth() const { return m_masonryColumnWidth; }

    WorkspaceItemState captureState(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);

    QString statusText() const;
    ImageMouseInfo mouseInfo() const { return m_mouseInfo; }
    QString currentPath() const;
    QSize imageSize() const;
    int itemCount() const;
    QStringList itemPaths() const;

signals:
    void statusChanged();
    void mouseInfoChanged(const ImageMouseInfo &info);
    void toolChanged(ImageView::Tool tool);
    /** Emitted when items are removed from the workspace (e.g. Delete key). */
    void workspacePathsChanged();
    /** Image mode: user activated previous / next via edge click. */
    void navigatePreviousRequested();
    void navigateNextRequested();
    /** Image mode: double-click requests fullscreen toggle. */
    void fullscreenToggleRequested();
    /**
     * Gallery layout: user clicked an item to open it in Image mode.
     * Path is the image file path.
     */
    void galleryItemOpenRequested(const QString &path);
    /** File URLs dropped onto the view (same semantics as MainWindow). */
    void filesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                      const QPointF &scenePos);

public slots:
    /** Deliver a finished background decode (generation must still match). */
    void onImageLoaded(const QString &path, const QImage &image, quint64 generation,
                       int role);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum LoadRole {
        LoadReplace = 0,
        LoadAdd = 1,
        LoadRestore = 2
    };

    ImageItem *createItemFromImage(const QString &path, const QImage &image);
    void scheduleImageLoad(const QString &path, LoadRole role);
    ImageItem *findItemByPath(const QString &path) const;
    ImageItem *primaryItem() const;
    ImageItem *targetItem() const;
    void updateMouseInfo(const QPoint &viewPos);
    void fitItem(ImageItem *item, Qt::AspectRatioMode mode = Qt::KeepAspectRatio);
    Qt::AspectRatioMode currentFitAspectMode() const;
    void ensureVisibleItem(ImageItem *item);
    qreal angleAt(const QPointF &scenePos, ImageItem *item) const;
    void rememberItemState(ImageItem *item);
    void snapshotWorkspace();
    void restoreWorkspace();
    void snapshotFreeFormStates();
    void restoreFreeFormStates();
    WorkspaceItemState defaultStateForPath(const QString &path, int ordinal) const;
    /** Centre position that does not overlap existing items (viewport-aware). */
    QPointF findEmptyPlacement(const QSizeF &itemSize) const;
    EdgeZone edgeZoneAt(const QPoint &viewPos) const;
    int edgeZoneWidth() const;
    void updateHoverEdge(const QPoint &viewPos);
    void drawEdgeAffordances(QPainter &painter);
    static QSizeF nativeSize(const ImageItem *item);
    void zoomViewBy(qreal factor);

    QGraphicsScene *m_scene = nullptr;
    QList<ImageItem *> m_items;
    /** Persistent per-path transforms while workspace mode is active. */
    QHash<QString, WorkspaceItemState> m_itemStates;
    /** Free-form positions restored when leaving a packaged layout. */
    QHash<QString, WorkspaceItemState> m_freeFormStates;
    /** View pan/zoom while in free-form; restored with the item states. */
    QTransform m_freeFormViewTransform;
    bool m_hasFreeFormViewTransform = false;
    QList<WorkspaceItemState> m_savedWorkspace;
    QString m_classicPath;

    QUndoStack *m_undoStack = nullptr;

    bool m_fitMode = true;
    bool m_fillMode = false;
    ViewMode m_viewMode = ViewMode::Image;
    bool m_imageModeNavEnabled = false;
    bool m_imageModeLeftDragPan = true;
    bool m_hudVisible = false;
    QString m_lastLoadError;
    bool m_hudFlashVisible = false;
    QString m_hudAction;
    QString m_hudDetail;
    QTimer *m_hudFlashTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutMode m_layoutMode = LayoutMode::FreeForm;
    int m_masonryColumnWidth = 240;
    std::atomic<quint64> m_loadGeneration{0};
    QSet<QString> m_pendingWorkspacePaths;
    /** Optional scene centre for in-flight LoadAdd decodes (e.g. drops). */
    QHash<QString, QPointF> m_pendingScenePos;
    ImageMouseInfo m_mouseInfo;

    QPoint m_lastMousePos;
    bool m_panning = false;

    bool m_rotating = false;
    ImageItem *m_rotateItem = nullptr;
    qreal m_rotateStartAngle = 0.0;
    qreal m_rotateItemStart = 0.0;

    ImageItem *m_dragItem = nullptr;
    WorkspaceItemState m_dragStartState;

    /** Gallery click-to-open: press on an item, release without dragging. */
    ImageItem *m_galleryPressItem = nullptr;
    QPoint m_galleryPressPos;
    bool m_galleryClickCandidate = false;
    bool m_applyingLayout = false;
    QTimer *m_layoutDebounceTimer = nullptr;
};

#endif // IMAGEVIEW_H
