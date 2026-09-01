// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "imageview.h"
#include "sessiondocument.h"
#include "imageview_types.h"

#include <QMainWindow>
#include <QUrl>
#include <QEvent>
#include <QStringList>
#include <QVector>
#include <QMimeData>

class ThumbnailBar;
class MetadataPanel;
class QDockWidget;
class QSplitter;
class QToolBar;
class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QSpinBox;
class QTimer;

/**
 * Snapshot of one session row for undo of remove.
 * Identity is @a id; path is decode source only. Appearance restores
 * ImageView store content (crop / content flips / quarter turns).
 */
struct SessionEntrySnapshot {
    int index = -1;
    QString path;
    SessionImageId id = kInvalidSessionImageId;
    WorkspaceItemState appearance;
    bool hasAppearance = false;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class SortMode {
        Name = 0,
        MTime = 1,
        FileSize = 2,
        Width = 3,
        Height = 4,
        PixelCount = 5
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** Replace the current session with the expanded paths. */
    void loadFiles(const QStringList &paths, int startAt = 0);

    /** Append expanded paths to the current session (deduplicated). */
    void appendFiles(const QStringList &paths);

    /** SessionRemoveCommand redo/undo (must be public — called from QUndoCommand). */
    void applySessionRemoveIndices(const QList<int> &indices);
    void restoreSessionEntries(const QList<SessionEntrySnapshot> &entries);
    /** Canvas + session duplicate; returns new SessionImageIds (for undo). */
    QVector<SessionImageId> applyDuplicate(const QStringList &sourcePaths);
    int sessionIndexOfId(SessionImageId id) const;
    bool writeProjectToPath(const QString &projectPath, QString *error = nullptr);
    bool loadProjectFromPath(const QString &projectPath, QString *error = nullptr);

    /** Clear session, canvas, and thumbnails (File → New). */
    void newSession();

    void setRecursive(bool recursive) { m_recursive = recursive; }
    void setSortMode(SortMode mode);
    void setThumbnailsForced(bool show) { m_forceThumbnails = show; m_forceNoThumbnails = !show; }
    void setNoThumbnailsForced(bool hide) { m_forceNoThumbnails = hide; if (hide) m_forceThumbnails = false; }
    void setSlideshowIntervalMs(int ms);
    void startSlideshow();
    void stopSlideshow();
    /**
     * CLI --mode: switch presentation after the window exists.
     * @p mode is "image", "gallery", or "workspace" (case-insensitive).
     * Gallery uses Masonry. Workspace places the full session on the canvas.
     * Unknown values are ignored.
     */
    void applyCliViewMode(const QString &mode);
    /** Shorter interval (faster); shortcut ]. */
    void slideshowFaster();
    /** Longer interval (slower); shortcut [. */
    void slideshowSlower();
    void printDocument();
    void printPreview();
    void pageSetup();
    void exportPdf();
    void exportPng();
    void togglePageGuide();
    void fitPageGuideToContent();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void openFiles();
    void addFiles();
    void openDirectory();
    /** F5: reload current image (Image) or re-decode gallery/workspace tiles. */
    void reloadFromDisk();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void zoomFill();
    void toggleFullscreen();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void toggleCropMode();
    void toggleHud();
    void toggleThumbnailLabels();
    void toggleThumbnailCrop();
    void goPrevious();
    void goNext();
    void goFirst();
    void goLast();
    void toggleSlideshow();
    void setLayoutFreeForm();
    void setLayoutSideBySide();
    void setLayoutVertical();
    void setLayoutGrid();
    void setLayoutGridCrop();
    void setLayoutMasonry();
    void setLayoutMasonryRows();
    void openGalleryItemInImageMode(const QString &path);
    /** Enter Image mode on a session row (preferred; duplicate-safe). */
    void openSessionIndexInImageMode(int sessionIndex);
    void openSessionImageInImageMode(SessionImageId sessionId);
    /** Up / top-edge: return to Gallery or Workspace after open-from-Image. */
    void returnFromImageMode();
    void returnToGallery();
    void returnToWorkspace();
    void opacityReset();
    void resetItemScale();
    void resetItemRotation();
    void duplicateSelected();
    void saveProject();
    void saveProjectAs();
    void openProject();
    /** Open selected canvas paths in a new MainWindow (WA_DeleteOnClose). */
    void openSelectionInNewWindow();
    void opacityDown();
    void opacityUp();
    void lowerSelected();
    void raiseSelected();
    void onSlideshowTick();
    void sortByName();
    void sortByMTime();
    void sortByFileSize();
    void sortByWidth();
    void sortByHeight();
    void sortByPixelCount();
    void toggleToolBar();
    void toggleThumbnailBar();
    void about();
    void showKeyboardShortcuts();
    void showPreferences();
    void onFilesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                        const QPointF &scenePos);
    void toggleScrollBars();
    void toggleWorkspaceMode();
    /** DOMAIN: enter Workspace (snapshot-aware via ImageView::setViewMode). */
    void enterWorkspaceMode();
    /** DOMAIN: enter Gallery with layout L and populate from session. */
    void enterGalleryMode(ImageView::LayoutMode layout);
    /** DOMAIN: show path in Image mode (session current = path). */
    void showPathInImageMode(const QString &path);
    bool isWorkspaceMode() const;
    bool isGalleryMode() const;
    bool isImageMode() const;
    void setSelectTool();
    void setPanTool();
    void updateWorkspaceActionVisibility();
    void updateUpToGalleryAction();
    void updateThumbnailBarForMode();
    void updateScrollBarPolicyForMode();
    void updateMasonryCountControl();
    void ensureMultiImageMode();
    /** Put every session image on the multi-image canvas (gallery). */
    void populateGalleryCanvas();
    void updateStatus();
    /** Refresh metadata dock from selection / session focus (deduped by path). */
    void updateMetadataPanel();
    void updateWindowTitle();
    void selectAllThumbnails();
    void onMouseInfoChanged(const ImageMouseInfo &info);
    void showContextMenu(const QPoint &pos);
    void onThumbnailActivated(int index);
    void onThumbnailAddToWorkspace(int index);
    void onThumbnailWorkspaceSelectionChanged();
    void onThumbnailCanvasMembershipToggled(int index);
    void onWorkspacePathsChanged();
    void removeSessionIndices(const QList<int> &indices);
    SessionImageId sessionIdAt(int index) const;
    int indexOfSessionId(SessionImageId id) const;
    SessionImageId currentSessionId() const;
    SessionImageId allocSessionId();
    void removeSessionPaths(const QStringList &paths);
    void removeSessionIds(const QVector<SessionImageId> &ids);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    /** Associate shortcut-bearing actions with the window (fullscreen-safe). */
    void bindViewerShortcuts();
    void updateFullscreenUi();
    /**
     * @p ensureGalleryVisible — when true (default), Gallery scrolls the
     * session tile into view. Mouse selection passes false so clicking a
     * tile does not jump the view; keyboard nav still scrolls via
     * focusSessionPath.
     */
    void setCurrentIndex(int index, bool ensureGalleryVisible = true);
    void updateNavigationActions();
    void applyThumbnailVisibility();
    enum class ThumbnailEdge { Bottom, Top, Left, Right };
    void setThumbnailBarPosition(ThumbnailEdge edge);
    void updateThumbnailEdgeActions();
    void sortFileList();
    void readSettings();
    void writeSettings();
    void rememberSessionHistory(const QStringList &paths);
    void rebuildHistoryMenu();
    void openHistoryEntry();
    void clearSessionHistory();
    QString historyEntryLabel(const QStringList &paths) const;
    void syncThumbnailWorkspaceSelection();
    void syncThumbnailCanvasMembership();
    /** Push thumbnail multi-select onto the canvas (Workspace membership / Gallery seed). */
    void syncCanvasFromThumbnailSelection();
    void showSlideshowCursor();
    void hideSlideshowCursor();
    void armSlideshowCursorHide();
    QStringList expandPaths(const QStringList &paths) const;
    QStringList extractLocalImagePaths(const QMimeData *mime) const;
    void handleDroppedUrls(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                           const QPointF &scenePos = QPointF());
    static bool isImageFile(const QString &path);

    ImageView *m_imageView = nullptr;
    ThumbnailBar *m_thumbnailBar = nullptr;
    bool m_syncingSelection = false;
    QSplitter *m_centralSplitter = nullptr;
    MetadataPanel *m_metadataPanel = nullptr;
    QString m_metadataPath;
    QDockWidget *m_metadataDock = nullptr;
    QToolBar *m_toolBar = nullptr;
    QToolBar *m_workspaceToolBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_mouseLabel = nullptr;
    QLabel *m_colorSwatch = nullptr;
    QTimer *m_slideshowTimer = nullptr;
    QTimer *m_cursorHideTimer = nullptr;
    bool m_slideshowCursorHidden = false;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_historyMenu = nullptr;
    QAction *m_clearHistoryAct = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_goMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_contextMenu = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_newAct = nullptr;
    QAction *m_addAct = nullptr;
    QAction *m_openDirAct = nullptr;
    QAction *m_reloadAct = nullptr;
    QAction *m_quitAct = nullptr;
    QAction *m_printAct = nullptr;
    QAction *m_printPreviewAct = nullptr;
    QAction *m_pageSetupAct = nullptr;
    QAction *m_exportPdfAct = nullptr;
    QAction *m_exportPngAct = nullptr;
    QAction *m_pageGuideAct = nullptr;
    QAction *m_fitPageGuideAct = nullptr;
    QAction *m_zoomInAct = nullptr;
    QAction *m_zoomOutAct = nullptr;
    QAction *m_zoom1to1Act = nullptr;
    QAction *m_zoomFitAct = nullptr;
    QAction *m_zoomFillAct = nullptr;
    QAction *m_zoomRegionAct = nullptr;
    QAction *m_fullscreenAct = nullptr;
    QAction *m_rotateLeftAct = nullptr;
    QAction *m_rotateRightAct = nullptr;
    QAction *m_flipHAct = nullptr;
    QAction *m_flipVAct = nullptr;
    QAction *m_cropAct = nullptr;
    QAction *m_toggleHudAct = nullptr;
    QAction *m_hideThumbLabelsAct = nullptr;
    QAction *m_cropThumbnailsAct = nullptr;
    QAction *m_previousAct = nullptr;
    QAction *m_nextAct = nullptr;
    QAction *m_firstAct = nullptr;
    QAction *m_lastAct = nullptr;
    QAction *m_slideshowAct = nullptr;
    QAction *m_slideshowFasterAct = nullptr;
    QAction *m_slideshowSlowerAct = nullptr;
    QAction *m_workspaceModeAct = nullptr;
    QAction *m_selectToolAct = nullptr;
    QAction *m_panToolAct = nullptr;
    QAction *m_selectAllAct = nullptr;
    QAction *m_undoAct = nullptr;
    QAction *m_redoAct = nullptr;
    QAction *m_layoutFreeFormAct = nullptr;
    QAction *m_layoutSideBySideAct = nullptr;
    QAction *m_layoutVerticalAct = nullptr;
    QAction *m_layoutGridAct = nullptr;
    QAction *m_layoutGridCropAct = nullptr;
    QAction *m_layoutMasonryAct = nullptr;
    QAction *m_layoutMasonryRowsAct = nullptr;
    QAction *m_backToGalleryAct = nullptr;
    QAction *m_masonryCountAction = nullptr;
    QSpinBox *m_masonryCountSpin = nullptr;
    QLabel *m_masonryCountLabel = nullptr;
    bool m_galleryReturnActive = false;
    /** Image was opened from Workspace (double-click); Up restores Workspace. */
    bool m_workspaceReturnActive = false;
    bool m_thumbsHiddenForGallery = false;
    bool m_thumbsVisibleBeforeGallery = false;
    ImageView::LayoutMode m_galleryReturnLayout = ImageView::LayoutMode::Masonry;
    QAction *m_opacityResetAct = nullptr;
    QAction *m_resetScaleAct = nullptr;
    QAction *m_resetRotationAct = nullptr;
    QAction *m_duplicateAct = nullptr;
    QAction *m_saveProjectAct = nullptr;
    QAction *m_saveProjectAsAct = nullptr;
    QAction *m_openProjectAct = nullptr;
    QAction *m_openSelectionNewWindowAct = nullptr;
    QAction *m_opacityDownAct = nullptr;
    QAction *m_opacityUpAct = nullptr;
    QAction *m_lowerAct = nullptr;
    QAction *m_raiseAct = nullptr;
    QAction *m_sortNameAct = nullptr;
    QAction *m_sortMTimeAct = nullptr;
    QAction *m_sortFileSizeAct = nullptr;
    QAction *m_sortWidthAct = nullptr;
    QAction *m_sortHeightAct = nullptr;
    QAction *m_sortPixelCountAct = nullptr;
    QAction *m_toggleToolBarAct = nullptr;
    QAction *m_toggleThumbnailBarAct = nullptr;
    QAction *m_thumbnailsBottomAct = nullptr;
    QAction *m_thumbnailsTopAct = nullptr;
    QAction *m_thumbnailsLeftAct = nullptr;
    QAction *m_thumbnailsRightAct = nullptr;
    QAction *m_toggleMetadataAct = nullptr;
    QAction *m_toggleScrollBarsAct = nullptr;
    QAction *m_preferencesAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QAction *m_keyboardShortcutsAct = nullptr;
    QActionGroup *m_sortGroup = nullptr;
    QActionGroup *m_thumbnailPositionGroup = nullptr;

    /** Working set: ordered paths + stable session-image ids (Phase 4). */
    SessionDocument m_session;
    QString m_projectPath;
    /** Past sessions (full path lists), newest first. */
    QList<QStringList> m_sessionHistory;
    static constexpr int kMaxSessionHistory = 20;
    int m_currentIndex = -1;
    bool m_recursive = false;
    ThumbnailEdge m_thumbnailEdge = ThumbnailEdge::Bottom;
    bool m_startInWorkspaceMode = false; // preference / startup default
    bool m_slideshowFullscreen = true;   // enter fullscreen when starting slideshow
    SortMode m_sortMode = SortMode::Name;
    bool m_sessionUndoGuard = false;
    int m_slideshowIntervalMs = 3000;
    bool m_forceThumbnails = false;
    bool m_forceNoThumbnails = false;
    bool m_slideshowAdvancing = false; // true while timer-driven next runs

    bool m_toolBarVisibleBeforeFullscreen = true;
    bool m_thumbnailBarVisibleBeforeFullscreen = true;
    bool m_metadataVisibleBeforeFullscreen = false;
};

#endif // MAINWINDOW_H
