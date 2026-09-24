// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "imageview.h"
#include "item/itemcomponents.h"
#include "session/sessiondocument.h"
#include "session/sessionsort.h"
#include "imageview_types.h"
#include "session/projectfile.h"

#include <QMainWindow>
#include <QLineEdit>
#include <QToolBar>
#include <functional>
#include <QProgressBar>
#include <QUrl>
#include <QEvent>
#include <QStringList>
#include <QVector>
#include <QHash>
#include <QDir>
#include <QMimeData>
#include <QElapsedTimer>

class DualImageShell;
class ThumbnailBar;
class MetadataPanel;
class AdjustmentsPanel;
class LayoutPanel;
class TocPanel;
class HelpPanel;
class QDockWidget;
class QToolBar;
class QAction;
class QActionGroup;
class QLabel;
class QCheckBox;
class QToolButton;
class QMenu;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /** Session list sort policy (owned by SessionSort). */
    using SortMode = SessionSort::Mode;

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** Replace the current session with the expanded paths. */
    void loadFiles(const QStringList &paths, int startAt = 0);

    /** Append expanded paths to the current session (deduplicated). */
    void appendFiles(const QStringList &paths);

    /** SessionRemoveCommand redo/undo (must be public — called from QUndoCommand). */
    void applySessionRemoveIndices(const QList<int> &indices);
    void removeSessionIndicesFromModel(const QList<int> &sorted);
    void refreshSessionUiAfterRemove();
    void selectIndexAfterSessionRemove(SessionImageId currentId, const QString &currentPath,
                                       const QList<int> &sorted);
    void restoreSessionEntries(const QList<SessionEntrySnapshot> &entries);
    /** Canvas + session duplicate; returns new SessionImageIds (for undo). */
    QVector<SessionImageId> applyDuplicate(const QList<SessionImageId> &sourceIds,
                                 const QStringList &fallbackPaths = {});
    /** Workspace paste/cut helpers for QUndoCommand. */
    QVector<SessionImageId> applyWorkspacePaste(const QList<WorkspaceItemState> &items);
    void applyWorkspaceCut(const QList<WorkspaceItemState> &items);
    void applyWorkspaceUncut(const QList<WorkspaceItemState> &items);
    void applyWorkspaceBackground(const WorkspaceBackground &bg);
    /** Called from Workspace paste/cut/background undo commands. */
    void markWorkspaceDirty();
    bool clipboardHasWorkspaceItems() const;
    int sessionIndexOfId(SessionImageId id) const;
    /** SessionReorderCommand redo/undo (must be public — called from QUndoCommand). */
    void applySessionOrder(const QStringList &paths,
                           const QVector<SessionImageId> &ids,
                           SessionImageId focusId = kInvalidSessionImageId);
    void selectSessionIdsOnFilmstrip(const QVector<SessionImageId> &ids);
    bool writeProjectToPath(const QString &projectPath, QString *error = nullptr);
    QString promptLocateMissingAsset(const ProjectAsset &asset,
                                     const ProjectImage &im,
                                     const QString &projectPath,
                                     bool *skipAllMissing);
    QString sessionPathFromResolvedAsset(const QString &resolved,
                                         const ProjectImage &im) const;
    bool resolveProjectSessionRows(const ProjectDocument &doc,
                                   const QString &projectPath,
                                   QStringList *paths,
                                   QVector<SessionImageId> *ids,
                                   QVector<WorkspaceItemState> *appearanceByRow,
                                   QVector<bool> *rowHasAppearance,
                                   QVector<bool> *rowHasPose,
                                   QStringList *missing);
    bool loadProjectFromPath(const QString &projectPath, QString *error = nullptr);
    /**
     * Load a .biltoo project from @a path, set the current project path,
     * and remember it in Recent Projects. On failure fills @a error (and
     * returns false) without changing the current project path.
     * Used by File → Open Project, Recent Projects, and the CLI.
     */
    bool openProjectFile(const QString &path, QString *error = nullptr);

    /** Clear session, canvas, and thumbnails (File → New). */
    void newSession();

    void setRecursive(bool recursive) { m_recursive = recursive; }
    void setSortMode(SortMode mode);
    void setThumbnailsForced(bool show) { m_forceThumbnails = show; m_forceNoThumbnails = !show; }
    void setNoThumbnailsForced(bool hide) { m_forceNoThumbnails = hide; if (hide) m_forceThumbnails = false; }
    void clampSlideshowTransitionToInterval();
    void rearmSlideshowAfterIntervalChange(int oldInterval);
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
    void exportDocumentText();
    void exportPng();
    /** Bake session rotate/flip/crop into new files (dir / cbz / pdf). */
    void exportSessionImages();
    void togglePageGuide();
    void fitPageGuideToContent();

protected:
    void closeEvent(QCloseEvent *event) override;
    /** GNOME2-style Close without Saving / Cancel / Save when Workspace is dirty. */
    bool confirmQuitOrClose();
    bool workspaceHasUnsavedWork() const;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void openFiles();
    void openLocation();
    void commitLocationBar();
    void cancelLocationBar();
    void setLocationBarPinned(bool pinned);
    void syncLocationBarText();
    void addFiles();
    void openDirectory();
    /** F5: reload current image (Image) or re-decode gallery/workspace tiles. */
    void reloadFromDisk();
    /** Shift+F5: clear host/tile caches for targets and re-decode from disk. */
    void hardReloadFromDisk();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void zoomFill();
    void syncZoomModeChecks();
    void toggleFullscreen();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    void resetContentAppearance();
    void toggleCropMode();
    void toggleAttentionMode();
    void openSearchBar();
    void cancelSearchBar();
    void setSearchBarPinned(bool pinned);
    void onSearchTextChanged(const QString &text);
    void updateSearchMatchLabel();
    void scheduleDocumentSearch(const QString &query);
    void startDocumentSearch(const QString &query);
    void onDocumentSearchFinished(quint64 generation, const QString &query,
                                  const QVector<int> &hitPages, int pageHits);
    void findNextMatch();
    void findPreviousMatch();
    void goToSearchHit(int index);
    QStringList documentPagePathsForSearch() const;
    void toggleHud();
    void toggleThumbnailLabels();
    void toggleThumbnailCrop();
    void goPrevious();
    void goNext();
    void goFirst();
    void goLast();
    void toggleSlideshow();
    void pauseSlideshow();
    void resumeSlideshow();
    void seekSlideshowFraction(qreal fraction);
    /** Playing or paused (not fully stopped). */
    bool isSlideshowSession() const;
    void updateSlideshowActionUi();
    /** After user next/prev during an active show: resync clock to new slide. */
    void onSlideshowUserNavigated();
    /** Start/restart the pure time-based slideshow clock and tick timer. */
    void armSlideshowAdvanceTimer();
    /**
     * After an interval change, keep the same slide and ~the same fraction of
     * the cycle (phaseT ∈ [0,1)) under the new interval so live settings edits
     * do not restart a full dwell (long pause on the current image).
     */
    void remapSlideshowPhase(int oldIntervalMs, int newIntervalMs);
    /** Sole tick handler: elapsed → cycle/phase → pure or transition (pure functions). */
    void updateSlideshowFromClock();
    void setLayoutFreeForm();
    void setLayoutSideBySide();
    void setLayoutVertical();
    void setLayoutGrid();
    void setLayoutGridCrop();
    void setLayoutMasonry();
    void setLayoutMasonryRows();
    void setLayoutMasonryFill();
    void setLayoutMasonryRowsFill();
    void setLayoutFlow();
    void setLayoutFlowFill();
    void setLayoutFacing();
    /** Toolbar primary click: enter Gallery with the last/current layout. */
    void goToGalleryCurrentLayout();
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
    void resetItemShear();
    void duplicateSelected();
    /** Workspace: copy/cut selected tiles; paste creates new session images. */
    void copyWorkspaceItems();
    void cutWorkspaceItems();
    void pasteWorkspaceItems();
    void editWorkspaceBackground();
    /** Session Gallery/Image canvas background (not Preferences, not project). */
    void editViewBackground();
    /**
     * Toolbar / View-menu Background entry: Workspace mode opens the project
     * Workspace Background dialog; Gallery/Image open the session View dialog.
     */
    void editBackgroundForCurrentMode();
    void workspaceBackgroundDefault(bool checked);
    void syncWorkspaceBackgroundActions();
    void updatePasteActionEnabled();
    void saveProject();
    void saveProjectAs();
    void openProject();
    /** Open filmstrip/canvas selection in a new MainWindow (WA_DeleteOnClose). */
    void openSelectionInNewWindow();
    /**
     * Session paths for current UI selection (filmstrip first, then canvas).
     * Prefer the larger multi-selection when strip and canvas disagree.
     */
    QStringList pathsFromUiSelection() const;
    /**
     * Ordered selection snapshots (path + optional content appearance) for
     * Open Selection in New Window. Preserves multiplicity and crop/flip/grade.
     */
    QList<SessionEntrySnapshot> sessionSelectionSnapshots() const;
    /**
     * Replace this window's session with @p entries (new SessionImageIds) and
     * install content appearance. Does not re-sort (preserves selection order).
     */
    void loadSessionSnapshots(const QList<SessionEntrySnapshot> &entries, int startAt = 0);
    /** Indices for Open Selection (prefer larger multi-select of strip vs canvas). */
    QList<int> sessionSelectionIndices() const;
    /** Spawn an empty MainWindow (File → New Window). */
    void newWindow();
    void opacityDown();
    void opacityUp();
    void lowerSelected();
    void raiseSelected();
    void onSlideshowTick(); // legacy name kept as private alias; use updateSlideshowFromClock
    void showSessionReorderDialog();
    void sortByName();
    void sortByPath();
    void sortByMTime();
    void sortByFileSize();
    void sortByWidth();
    void sortByHeight();
    void sortByPixelCount();
    void sortByAspectRatio();
    void sortByShuffle();
    void toggleToolBar();
    void toggleThumbnailBar();
    void about();
    void showKeyboardShortcuts();
    void showPreferences();
    bool resolveEpubLayoutTarget(QString *epubFile, QString *layoutParams,
                                 int *keepPage) const;
    void rewriteEpubSessionPaths(const QString &epubFile, const QString &newParams,
                                 int keepPage);
    void showEpubLayoutDialog();
    /** Current PDF (page/file/pdfimage) → session of //pdfimage:N embeds. */
    void openPdfAsEmbeddedImages();
    void showSlideshowSettings();
    void onFilesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                        const QPointF &scenePos, bool hasScenePos,
                        const QList<qint64> &sessionIds = {},
                        const QStringList &internalPaths = {});
    void toggleScrollBars();
    void toggleWorkspaceMode();
    /** Stage 2c.2: Image-mode side-by-side compare (shared ItemWorld + pipeline). */
    void setDualCompareEnabled(bool on);

    /** DOMAIN: enter Workspace (snapshot-aware via ImageView::setViewMode). */
    void enterWorkspaceMode();
    /** DOMAIN: enter Gallery with layout L and populate from session. */
    void enterGalleryMode(LayoutMode layout);
    /** DOMAIN: show path in Image mode (session current = path). */
    void showPathInImageMode(const QString &path);
    bool isWorkspaceMode() const;
    bool isGalleryMode() const;
    bool isImageMode() const;
    void setSelectTool();
    void setPanTool();
    void setZoomTool();
    void updateWorkspaceActionVisibility();
    void updateUpToGalleryAction();
    void updateThumbnailBarForMode();
    /** Apply Layout dock visibility/enablement for the current view mode. */
    void updateLayoutPanelForMode();
    void updateFileExportActions();
    void updateScrollBarPolicyForMode();
    void updateMasonryCountControl();
    /** Sync exclusive layout action checks + toolbar combo icon/tooltip. */
    void syncGalleryLayoutUi(LayoutMode layout);
    /** Put every session image on the multi-image canvas (gallery). */
    void populateGalleryCanvas();
    void updateStatus();
    void ensureWorkStatusPoll(bool workBusy);
    void refreshWorkActivityStatusBar();
    /** Refresh metadata dock from selection / session focus (deduped by path). */
    void updateMetadataPanel();
    void updateTocPanel();
    void navigateDocumentPage(int page_1based);
    void openDocumentLinkUri(const QString &uri);
    void updateAdjustmentsPanel();
    void updateLayoutPanel();
    void applyWorkspaceLayoutFromPanel();
    void updateWindowTitle();
    void selectAllThumbnails();
    void onMouseInfoChanged(const ImageMouseInfo &info);
    void showContextMenu(const QPoint &pos);
    void onThumbnailActivated(int index);
    void onThumbnailNavigated(int index);
    void onThumbnailWorkspaceSelectionChanged();
    void onWorkspacePathsChanged();
    void removeSessionIndices(const QList<int> &indices);
    SessionImageId sessionIdAt(int index) const;
    int indexOfSessionId(SessionImageId id) const;
    /**
     * Session list index for @p path: first bound SessionImageId for the path,
     * else paths().indexOf (fully unbound rows only).
     */
    int indexOfPathPreferId(const QString &path) const;
    SessionImageId currentSessionId() const;
    SessionImageId allocSessionId();
    void removeSessionPaths(const QStringList &paths);
    void removeSessionIds(const QVector<SessionImageId> &ids);

private:
    void createActions();
    void createMenus();
    /** Connect QAction::hovered / triggered → Help panel for all actions. */
    void installActionHelpTracking();
    void installMenuHelpTracking(QMenu *menu);
    /** Resolve action under cursor for Help panel (toolbars / menus). */
    void updateHelpPanelFromWidget(QWidget *widget, const QPoint &localPos);
    /** Seed QAction::whatsThis() for commands that already have long help. */
    void populateActionHelpTexts();
    /** Help panel topic for the current presentation mode (Image/Gallery/Workspace). */
    void showCurrentModeHelp();
    void showFilmstripHelp();
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
    bool refreshSameCurrentIndex(bool ensureGalleryVisible);
    void publishSessionCursorForIndex(int index);
    void applyCurrentIndexCanvasChange(const QString &path, bool ensureGalleryVisible);
    void finishCurrentIndexChromeUpdate();
    void updateNavigationActions();
    void updateNavPrevNextSlideshowActions(bool hasFiles, bool hasMany);
    void updateNavTransformCropActions(bool canTransform);
    void updateNavZoomAndSelectionActions(bool hasFiles, bool hasItem);
    void applyThumbnailVisibility();
    enum class ThumbnailEdge { Bottom, Top, Left, Right };
    void setThumbnailBarPosition(ThumbnailEdge edge);
    void onThumbnailDockLocationChanged(Qt::DockWidgetArea area);
    void updateThumbnailEdgeActions();
    /** Name / mtime / file size — no image I/O. */
    void sortFileListSync();
    /** Width / height / pixels: probe off the GUI thread, then apply order. */
    void sortFileListWithProbesInBackground(const std::function<void()> &onDone = {});
    void applySortedSessionOrder(const QStringList &newFiles,
                                 const QVector<SessionImageId> &newIds,
                                 const std::function<void()> &onDone);
    /**
     * Move @p rows so they land starting at @p insertBefore in the list after
     * the moved rows are removed. Undoable via host undo stack.
     */
    void reorderSessionRows(const QList<int> &rows, int insertBefore);
    bool sortModeNeedsImageProbe() const;
    /** Majority of paths are PDF/EPUB/DjVu page (or pdfimage) refs. */
    static bool sessionLooksLikePagedDocument(const QStringList &paths);
    /** Flow for paged documents; otherwise last Gallery layout preference. */
    LayoutMode initialGalleryLayoutForOpen() const;
    void readSettings();
    void writeSettings();
    void rememberSessionHistory(const QStringList &paths);
    void rebuildHistoryMenu();
    void openHistoryEntry();
    void clearSessionHistory();
    void rememberRecentProject(const QString &path);
    void rebuildRecentProjectsMenu();
    void openRecentProject();
    void clearRecentProjects();
    QString historyEntryLabel(const QStringList &paths) const;
    /** Rich Help-panel body for one remembered session (file list). */
    QString historyEntryHelpHtml(const QStringList &paths) const;
    void syncThumbnailCanvasMembership();
    /** Push thumbnail multi-select onto the canvas (Workspace membership / Gallery seed). */
    void showSlideshowCursor();
    void hideSlideshowCursor();
    void armSlideshowCursorHide();
    QStringList expandPaths(const QStringList &paths) const;

    /** True when expansion may touch archives (run off the GUI thread). */
    bool pathsNeedBackgroundExpand(const QStringList &paths) const;
    /**
     * Expand paths on a worker thread, then load or append the result.
     * @p append false → replace session (loadFiles); true → appendFiles.
     */
    void expandPathsInBackground(const QStringList &paths, bool append, int startAt = 0);
    void applyExpandedPathsResult(const QStringList &images, bool append, int startAt,
                                  const QStringList &sourcePaths);
    /**
     * After paths/ids/appearance rows are resolved from a .biltoo document,
     * install session, mode, Workspace poses, and background.
     */
    void installProjectAppearances(const QVector<SessionImageId> &ids,
                                   const QVector<WorkspaceItemState> &appearanceByRow,
                                   const QVector<bool> &rowHasAppearance);
    void installProjectBackground(const ProjectDocument &doc, const QString &projectPath);
    void enterProjectCanvasMode(const QStringList &paths,
                                const QVector<SessionImageId> &ids,
                                const QVector<WorkspaceItemState> &appearanceByRow,
                                const QVector<bool> &rowHasPose,
                                const ProjectDocument &doc,
                                int poseCount);
    void finishProjectInstall(const QStringList &missing);
    void installProjectSession(const QStringList &paths,
                               QVector<SessionImageId> ids,
                               QVector<WorkspaceItemState> appearanceByRow,
                               const QVector<bool> &rowHasAppearance,
                               const QVector<bool> &rowHasPose,
                               const ProjectDocument &doc,
                               const QString &projectPath,
                               const QStringList &missing);

    // --- project write helpers ---
    QString currentProjectModeString() const;
    static QString containerHashPathForSessionPath(const QString &sessionPath);
    QString ensureProjectAsset(ProjectDocument *doc, QHash<QString, QString> *pathToSha,
                               const QDir &projDir, const QString &sessionPath);
    QHash<SessionImageId, ItemComponents::Placement> captureLiveWorkspacePoses() const;
    static void mergePoseIntoProjectImage(ProjectImage *im, const ItemComponents::Placement &pose);
    void attachWorkspaceBackgroundToDocument(ProjectDocument *doc, const QDir &projDir);
    void setExpandProgressMessage(const QString &message);
    void setExpandProgressBusy(bool busy);
    void applyExpandedLoad(const QStringList &images, int startAt);
    void applyExpandedAppend(const QStringList &images);
    void finishExpandedAppendChrome(SessionImageId currentId, const QString &currentPath,
                                    const QStringList &workspacePaths,
                                    const QVector<SessionImageId> &workspaceIds);
    void finishApplyExpandedLoad(int startAt);
    void setExpandProgress(int current, int total, const QString &message);
    QStringList extractLocalImagePaths(const QMimeData *mime) const;
    void handleWorkspaceDrop(const QStringList &paths, bool fromInternalSelection,
                             const QPointF &scenePos, bool hasScenePos,
                             const QList<qint64> &sessionIds);
    void handleGalleryDrop(const QStringList &paths, bool fromInternalSelection,
                            const QList<qint64> &sessionIds = {},
                            const QPointF &scenePos = {}, bool hasScenePos = false);
    /** Gallery session-row insert index from drop scene position (0..size). */
    int galleryReorderInsertBefore(const QPointF &scenePos) const;
    void handleImageModeDrop(const QStringList &paths, bool fromInternalSelection,
                             const QList<qint64> &sessionIds = {});
    void handleDroppedUrls(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                           const QPointF &scenePos, bool hasScenePos,
                           const QList<qint64> &sessionIds = {},
                           const QStringList &internalPaths = {});
    static bool isImageFile(const QString &path);

    ImageView *m_imageView = nullptr;
    /** Stage 2c.2: owns splitter; primary is m_imageView. */
    DualImageShell *m_dualShell = nullptr;
    QAction *m_dualCompareAct = nullptr;
    ThumbnailBar *m_thumbnailBar = nullptr;
    QDockWidget *m_thumbnailDock = nullptr;
    bool m_dockLocationGuard = false;
    bool m_syncingSelection = false;
    MetadataPanel *m_metadataPanel = nullptr;
    AdjustmentsPanel *m_adjustmentsPanel = nullptr;
    QDockWidget *m_adjustmentsDock = nullptr;
    /** Debounce histogram/vectorscope rebuild while colour sliders move. */
    QTimer *m_adjustmentsPreviewTimer = nullptr;
    QAction *m_toggleAdjustmentsAct = nullptr;
    QString m_metadataPath;
    QDockWidget *m_metadataDock = nullptr;
    LayoutPanel *m_layoutPanel = nullptr;
    QDockWidget *m_layoutDock = nullptr;
    QDockWidget *m_tocDock = nullptr;
    TocPanel *m_tocPanel = nullptr;
    HelpPanel *m_helpPanel = nullptr;
    QDockWidget *m_helpDock = nullptr;
    QToolBar *m_toolBar = nullptr;
    QToolBar *m_workspaceToolBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_statusProgress = nullptr;
    /** Cancels stale archive-expand workers when a newer open starts. */
    quint64 m_expandGeneration = 0;
    quint64 m_sortGeneration = 0;
    QLabel *m_mouseLabel = nullptr;
    QLabel *m_colorSwatch = nullptr;
    QTimer *m_slideshowTimer = nullptr;
    /** Debounced neighbour preload after rapid ←/→. */
    QTimer *m_slideshowPreloadTimer = nullptr;
    QTimer *m_slideshowNavLoadTimer = nullptr;
    QTimer *m_cursorHideTimer = nullptr;
    bool m_slideshowCursorHidden = false;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_historyMenu = nullptr;
    QAction *m_clearHistoryAct = nullptr;
    QMenu *m_recentProjectsMenu = nullptr;
    QAction *m_clearRecentProjectsAct = nullptr;
    QMenu *m_editMenu = nullptr;
    QMenu *m_imageMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_goMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_contextMenu = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_newAct = nullptr;
    QAction *m_newWindowAct = nullptr;
    QAction *m_addAct = nullptr;
    QAction *m_openDirAct = nullptr;
    QAction *m_reloadAct = nullptr;
    QAction *m_hardReloadAct = nullptr;
    QAction *m_openLocationAct = nullptr;
    QAction *m_showLocationBarAct = nullptr;
    QToolBar *m_locationBar = nullptr;
    QLineEdit *m_locationEdit = nullptr;
    bool m_locationBarPinned = false;
    QToolBar *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchMatchLabel = nullptr;
    QCheckBox *m_searchFuzzyCheck = nullptr;
    QToolButton *m_searchPrevBtn = nullptr;
    QToolButton *m_searchNextBtn = nullptr;
    bool m_searchBarPinned = false;
    QTimer *m_docSearchDebounce = nullptr;
    quint64 m_docSearchGeneration = 0;
    QString m_docSearchQuery;
    /** 1-based page numbers with ≥1 match (document-wide scan). */
    QVector<int> m_docSearchHitPages;
    int m_docSearchHitIndex = -1;
    int m_docSearchPageMatchCount = 0;
    bool m_docSearchRunning = false;
    QAction *m_quitAct = nullptr;
    QAction *m_printAct = nullptr;
    QAction *m_printPreviewAct = nullptr;
    QAction *m_pageSetupAct = nullptr;
    QAction *m_exportPdfAct = nullptr;
    QAction *m_exportTextAct = nullptr;
    QAction *m_exportPngAct = nullptr;
    QAction *m_exportSessionImagesAct = nullptr;
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
    QAction *m_resetContentAppearanceAct = nullptr;
    QAction *m_cropAct = nullptr;
    QAction *m_attentionAct = nullptr;
    QAction *m_toggleHudAct = nullptr;
    QAction *m_toggleContentEditMarksAct = nullptr;
    QAction *m_showTextRegionsAct = nullptr;
    QAction *m_findOnPageAct = nullptr;
    QAction *m_showSearchBarAct = nullptr;
    QAction *m_hideThumbLabelsAct = nullptr;
    QAction *m_cropThumbnailsAct = nullptr;
    QAction *m_previousAct = nullptr;
    QAction *m_nextAct = nullptr;
    QAction *m_firstAct = nullptr;
    QAction *m_lastAct = nullptr;
    QAction *m_slideshowAct = nullptr;
    QAction *m_slideshowSettingsAct = nullptr;
    QAction *m_slideshowFasterAct = nullptr;
    QAction *m_slideshowSlowerAct = nullptr;
    QAction *m_workspaceModeAct = nullptr;
    QAction *m_selectToolAct = nullptr;
    QAction *m_panToolAct = nullptr;
    QAction *m_zoomToolAct = nullptr;
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
    QAction *m_layoutMasonryFillAct = nullptr;
    QAction *m_layoutMasonryRowsFillAct = nullptr;
    QAction *m_layoutFlowAct = nullptr;
    QAction *m_layoutFlowFillAct = nullptr;
    QAction *m_layoutFacingAct = nullptr;
    /** Toolbar MenuButtonPopup default action: Go to Gallery (icon tracks layout). */
    QAction *m_galleryLayoutToolbarAct = nullptr;
    QAction *m_backToGalleryAct = nullptr;
    QAction *m_masonryCountAction = nullptr;
    QSpinBox *m_masonryCountSpin = nullptr;
    QLabel *m_masonryCountLabel = nullptr;
    bool m_pendingGalleryCrop = false;
    bool m_galleryReturnActive = false;
    /** Image was opened from Workspace (e.g. double-click a canvas tile); Back restores Workspace. */
    bool m_workspaceReturnActive = false;
    /** Preferred thumbnail-bar visibility when in Workspace (default on). */
    bool m_thumbnailsPreferredWorkspace = true;
    /** Preferred thumbnail-bar visibility when in Gallery (default off). */
    bool m_thumbnailsPreferredGallery = false;
    /** Preferred Layout dock visibility when in Workspace (default off). */
    bool m_layoutPreferredInWorkspace = false;
    LayoutMode m_galleryReturnLayout = LayoutMode::Masonry;
    QAction *m_opacityResetAct = nullptr;
    QAction *m_resetScaleAct = nullptr;
    QAction *m_resetRotationAct = nullptr;
    QAction *m_resetShearAct = nullptr;
    QAction *m_duplicateAct = nullptr;
    QAction *m_copyWorkspaceAct = nullptr;
    QAction *m_cutWorkspaceAct = nullptr;
    QAction *m_pasteWorkspaceAct = nullptr;
    QAction *m_workspaceBackgroundAct = nullptr;
    QAction *m_workspaceBgDefaultAct = nullptr;
    QAction *m_viewBackgroundAct = nullptr;
    QAction *m_saveProjectAct = nullptr;
    QAction *m_saveProjectAsAct = nullptr;
    QAction *m_openProjectAct = nullptr;
    QAction *m_openSelectionNewWindowAct = nullptr;
    QAction *m_opacityDownAct = nullptr;
    QAction *m_opacityUpAct = nullptr;
    QAction *m_lowerAct = nullptr;
    QAction *m_raiseAct = nullptr;
    QAction *m_reorderSessionAct = nullptr;
    QAction *m_sortNameAct = nullptr;
    QAction *m_sortPathAct = nullptr;
    QAction *m_sortAspectAct = nullptr;
    QAction *m_sortShuffleAct = nullptr;
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
    QAction *m_toggleTocAct = nullptr;
    QAction *m_toggleHelpAct = nullptr;
    QAction *m_toggleLayoutPanelAct = nullptr;
    QAction *m_toggleScrollBarsAct = nullptr;
    QAction *m_preferencesAct = nullptr;
    QAction *m_epubLayoutAct = nullptr;
    QAction *m_pdfEmbeddedImagesAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QAction *m_keyboardShortcutsAct = nullptr;
    /** Help → Guides: mode / chrome overviews (not toolbar commands). */
    QAction *m_helpGuideImageAct = nullptr;
    QAction *m_helpGuideGalleryAct = nullptr;
    QAction *m_helpGuideWorkspaceAct = nullptr;
    QAction *m_helpGuideFilmstripAct = nullptr;
    QAction *m_helpGuideSessionAct = nullptr;
    QActionGroup *m_sortGroup = nullptr;
    QActionGroup *m_thumbnailPositionGroup = nullptr;

    /** Working set: ordered paths + stable session-image ids (Phase 4). */
    SessionDocument m_session;
    QString m_projectPath;
    /** True after Workspace canvas changes until successful project save/load. */
    bool m_workspaceDirty = false;
    /** Stacked paste offset (reset on Copy/Cut); each Paste steps by 40px. */
    int m_workspacePasteGeneration = 0;
    /** Past sessions (full path lists), newest first. */
    QList<QStringList> m_sessionHistory;
    static constexpr int kMaxSessionHistory = 20;
    QStringList m_recentProjects;
    static constexpr int kMaxRecentProjects = 12;
    int m_currentIndex = -1;
    bool m_recursive = false;
    ThumbnailEdge m_thumbnailEdge = ThumbnailEdge::Bottom;
    bool m_startInWorkspaceMode = false; // preference / startup default
    bool m_slideshowFullscreen = true;   // enter fullscreen when starting slideshow
    bool m_slideshowLoop = true;          // last → first; false = stop after last
    /** True if this slideshow session called showFullScreen(); Esc/stop restores window. */
    bool m_slideshowOwnsFullscreen = false;
    SortMode m_sortMode = SortMode::Name;
    bool m_sessionUndoGuard = false;
    int m_slideshowIntervalMs = 3000;
    bool m_forceThumbnails = false;
    bool m_forceNoThumbnails = false;
    /** True while status bar shows drag/gallery decode progress (clear when 0). */
    bool m_decodeStatusActive = false;
    bool m_inUpdateStatus = false;
    /** Delay clearing the Loading… status so transient 0 does not flicker. */
    QTimer *m_decodeStatusClearTimer = nullptr;
    /** Polls thumtoo WorkActivity while busy (~5 Hz) so the status bar stays live. */
    QTimer *m_workStatusTimer = nullptr;
    bool m_slideshowAdvancing = false; // true while timer-driven next runs
    bool m_slideshowPaused = false; // session active, timer stopped, framing kept
    /** Pure time base: elapsed since start (minus paused gaps). */
    QElapsedTimer m_slideshowClock;
    qint64 m_slideshowPausedAccumMs = 0;
    /**
     * Continuous slideshow position in "slide units": integer part is cycles
     * since arm (added to base index), fractional part is phase in [0,1) within
     * the current cycle. Interval only scales wall-time → d(position)/dt —
     * never needs remap on speed change.
     */
    qreal m_slideshowPosition = 0.0;
    /** Session index at clock zero; cycle adds on top. */
    int m_slideshowBaseIndex = 0;
    /** Cycle number for which we already started a transition (-1 = none). */
    qint64 m_slideshowTransitionCycle = -1;
    /** Target session index for the in-flight transition (-1 = none). */
    int m_slideshowPendingToIndex = -1;
    /** Last toIdx for which look-ahead preloadSlideshowImage ran (clock tick gate). */
    int m_slideshowPreloadToIdx = -1;
    /** True while the repeating clock tick is the authority (playing). */
    bool m_slideshowClockRunning = false;

    bool m_toolBarVisibleBeforeFullscreen = true;
    bool m_thumbnailBarVisibleBeforeFullscreen = true;
    bool m_metadataVisibleBeforeFullscreen = false;
    bool m_layoutVisibleBeforeFullscreen = false;
    bool m_adjustmentsVisibleBeforeFullscreen = false;
    bool m_helpVisibleBeforeFullscreen = false;
};

#endif // MAINWINDOW_H
