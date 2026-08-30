// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef IMAGEVIEW_H
#define IMAGEVIEW_H

#include "imageview_types.h"

#include <QColor>
#include <QGraphicsView>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <atomic>

class ImageItem;
class QGraphicsScene;
class QTimer;
class QUndoStack;

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

    enum class BackgroundPattern {
        Solid,
        Checkerboard
    };

    enum class ViewMode {
        Image,
        Gallery,
        Workspace
    };

    /** Hover / hit zone on viewport edges in Image mode. */
    enum class EdgeZone {
        None,
        Previous,
        Next,
        /** Top edge: return to Gallery when opened from a gallery tile. */
        GalleryReturn
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
    void prepareGalleryCanvas();
    ViewMode viewMode() const { return m_viewMode; }
    bool isImageMode() const { return m_viewMode == ViewMode::Image; }
    bool isGalleryMode() const { return m_viewMode == ViewMode::Gallery; }
    bool isWorkspaceMode() const { return m_viewMode == ViewMode::Workspace; }
    bool isMultiItemMode() const { return m_viewMode != ViewMode::Image; }

    /** @deprecated Alias for isWorkspaceMode(). */
    bool workspaceMode() const { return isWorkspaceMode(); }

    void clearExtras();

    /**
     * Enable left/right edge navigation affordances in Image mode.
     * Typically true when the session has more than one image.
     */
    void setImageModeNavigationEnabled(bool on);
    /** When true, Image mode top edge offers return-to-gallery. */
    void setGalleryReturnAvailable(bool on);
    bool galleryReturnAvailable() const { return m_galleryReturnAvailable; }
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

    void setBackgroundColor(const QColor &color);
    QColor backgroundColor() const { return m_bgColor; }
    void setBackgroundColorAlt(const QColor &color);
    QColor backgroundColorAlt() const { return m_bgColorAlt; }
    void setBackgroundPattern(BackgroundPattern pattern);
    BackgroundPattern backgroundPattern() const { return m_bgPattern; }
    /** When true, checkerboard is used only in Workspace; other modes stay solid. */
    void setCheckerboardWorkspaceOnly(bool on);
    bool checkerboardWorkspaceOnly() const { return m_bgCheckerWorkspaceOnly; }

    /**
     * Session position for status line and HUD ([index/total], 1-based display).
     * Pass total <= 1 or index < 0 to hide the prefix.
     */
    void setSessionPosition(int index, int total);
    /** Select canvas item for @p path; ensure visible in Gallery. */
    void focusSessionPath(const QString &path);
    int sessionIndex() const { return m_sessionIndex; }
    int sessionTotal() const { return m_sessionTotal; }

    /** Pin the on-image HUD overlay (filename, zoom, …). */
    void setHudVisible(bool on);
    bool hudVisible() const { return m_hudVisible; }

    /**
     * Brief top-left HUD action (slideshow, fit mode, …).
     * Visible for ~1.8s; never used for Next/Prev (session badge covers that).
     */
    void flashHud(const QString &action, const QString &detail = QString());

    /** Invoked by ImageItem during handle interaction for live status updates. */
    Q_INVOKABLE void refreshStatus();

    void raiseSelected();
    void lowerSelected();
    void opacityUp();
    void opacityDown();
    void opacityReset();
    void resetItemScale();
    void resetItemRotation();
    /** Workspace: clone selection (same path, independent transforms). */
    void duplicateSelected();

    /**
     * Arrangement of items. FreeForm is used only in Workspace mode.
     * Other values are Gallery layouts.
     */
    enum class LayoutMode {
        FreeForm,
        SideBySide, // horizontal strip (UI: "Horizontal")
        Vertical,   // vertical strip
        Grid,
        /** Square cells; image scaled to cover and centre-cropped (like thumb crop). */
        GridCrop,
        /** Column masonry: N columns spanning the view width; variable row heights. */
        Masonry,
        /** Row masonry: N rows spanning the view height; variable column widths. */
        MasonryRows
    };

    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }
    void applyLayout();
    void applyPendingGalleryRestore();
    /** Coalesce resize-driven gallery relayouts. */
    void scheduleApplyLayout();

    /** Gallery mode with a packaged layout. */
    bool isGalleryLayout() const { return isGalleryMode(); }

    /** Enter Gallery mode and apply the given packaged layout (not FreeForm). */
    void enterGallery(LayoutMode packagedLayout);

    /** Remember gallery scroll position (call before leaving Gallery for Image). */
    void snapshotGalleryViewport();
    /**
     * After gallery items are laid out, select @p focusPath (if present) and
     * restore the last snapshot scroll, then ensure the focused item is visible.
     */
    void restoreGalleryViewport(const QString &focusPath = QString());

    /** Number of columns for LayoutMode::Masonry (images scale to fit column width). */
    void setMasonryColumns(int columns);
    int masonryColumns() const { return m_masonryColumns; }
    /** Grid / GridCrop columns; 0 = automatic. */
    void setGridColumns(int columns);
    int gridColumns() const { return m_gridColumns; }

    /** Number of rows for LayoutMode::MasonryRows (images scale to fit row height). */
    void setMasonryRows(int rows);
    int masonryRows() const { return m_masonryRows; }

    WorkspaceItemState captureState(const ImageItem *item) const;
    void applyState(ImageItem *item, const WorkspaceItemState &state);

    QString statusText() const;
    /** Session badge for the top-right HUD, e.g. "[3/12]", or empty. */
    QString sessionBadgeText() const;
    /** Basename of the current/target image for the bottom HUD. */
    QString hudFileName() const;
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
    /** Image mode: user activated top-edge return to Gallery. */
    void galleryReturnRequested();
    /** Image mode: double-click requests fullscreen toggle. */
    void fullscreenToggleRequested();
    /**
     * Gallery layout: user clicked an item to open it in Image mode.
     * Path is the image file path.
     */
    void galleryItemOpenRequested(const QString &path);
    /** Gallery keyboard focus moved to this path (session cursor). */
    void galleryItemFocused(const QString &path);
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
    void drawForeground(QPainter *painter, const QRectF &rect) override;
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
    /** Interactive / gallery / static flags for the current ViewMode. */
    void applyItemModeFlags(ImageItem *item);
    void scheduleImageLoad(const QString &path, LoadRole role);
    ImageItem *findItemByPath(const QString &path) const;
    ImageItem *primaryItem() const;
    ImageItem *targetItem() const;
    void updateMouseInfo(const QPoint &viewPos);
    /** Frame @p item in the view. Image mode: does not clear rotation/flips. */
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
    int edgeZoneHeight() const;
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
    bool m_galleryReturnAvailable = false;
    /** Gallery: path under cursor for HUD filename (empty when none). */
    QString m_galleryHoverPath;
    bool m_imageModeLeftDragPan = true;
    QColor m_bgColor{42, 42, 42};
    QColor m_bgColorAlt{48, 48, 48};
    BackgroundPattern m_bgPattern = BackgroundPattern::Checkerboard;
    bool m_bgCheckerWorkspaceOnly = true;
    bool m_hudVisible = false;
    int m_sessionIndex = -1;
    int m_sessionTotal = 0;
    QString m_lastLoadError;
    bool m_hudFlashVisible = false;
    /** Filename + index shown briefly after navigation / flash (not only when pinned). */
    bool m_hudIdentityPulse = false;
    QString m_hudAction;
    QString m_hudDetail;
    QTimer *m_hudFlashTimer = nullptr;
    EdgeZone m_hoverEdge = EdgeZone::None;
    Tool m_tool = Tool::Select;
    LayoutMode m_layoutMode = LayoutMode::FreeForm;
    int m_masonryColumns = 3;
    int m_gridColumns = 0;
    int m_masonryRows = 3;
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

    ImageItem *m_handleDragItem = nullptr;
    ImageItem *m_dragItem = nullptr;
    WorkspaceItemState m_dragStartState;

    /** Gallery click-to-open: press on an item, release without dragging. */
    /** Anchor for Shift+click range selection in Gallery (session order). */
    ImageItem *m_gallerySelectionAnchor = nullptr;
    bool m_applyingLayout = false;
    QTimer *m_layoutDebounceTimer = nullptr;
    int m_galleryScrollH = 0;
    int m_galleryScrollV = 0;
    bool m_haveGalleryScroll = false;
    QString m_galleryFocusPath;
    bool m_pendingGalleryRestore = false;
};

#endif // IMAGEVIEW_H
