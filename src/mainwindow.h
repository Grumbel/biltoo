// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "imageview.h"

#include <QMainWindow>
#include <QEvent>
#include <QStringList>

class ThumbnailBar;
class QToolBar;
class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QTimer;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum class SortMode {
        Name,
        MTime
    };

    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /** Replace the current session with the expanded paths. */
    void loadFiles(const QStringList &paths, int startAt = 0);

    /** Append expanded paths to the current session (deduplicated). */
    void appendFiles(const QStringList &paths);

    void setRecursive(bool recursive) { m_recursive = recursive; }
    void setSortMode(SortMode mode);
    void setThumbnailsForced(bool show) { m_forceThumbnails = show; m_forceNoThumbnails = !show; }
    void setNoThumbnailsForced(bool hide) { m_forceNoThumbnails = hide; if (hide) m_forceThumbnails = false; }
    void setSlideshowIntervalMs(int ms);
    void startSlideshow();
    void stopSlideshow();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private slots:
    void openFiles();
    void addFiles();
    void openDirectory();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void toggleFullscreen();
    void rotateLeft();
    void rotateRight();
    void goPrevious();
    void goNext();
    void toggleSlideshow();
    void layoutSideBySide();
    void onThumbnailDoubleClicked(int index);
    void clearWorkspaceExtras();
    void opacityReset();
    void opacityDown();
    void opacityUp();
    void lowerSelected();
    void raiseSelected();
    void onSlideshowTick();
    void sortByName();
    void sortByMTime();
    void toggleToolBar();
    void toggleThumbnailBar();
    void about();
    void showPreferences();
    void updateStatus();
    void onMouseInfoChanged(const ImageMouseInfo &info);
    void showContextMenu(const QPoint &pos);
    void onThumbnailActivated(int index);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void updateFullscreenUi();
    void setCurrentIndex(int index);
    void updateNavigationActions();
    void applyThumbnailVisibility();
    void sortFileList();
    void readSettings();
    void writeSettings();
    QStringList expandPaths(const QStringList &paths) const;
    QStringList extractLocalImagePaths(const QMimeData *mime) const;
    static bool isImageFile(const QString &path);

    ImageView *m_imageView = nullptr;
    ThumbnailBar *m_thumbnailBar = nullptr;
    QToolBar *m_toolBar = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_mouseLabel = nullptr;
    QTimer *m_slideshowTimer = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_goMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_contextMenu = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_addAct = nullptr;
    QAction *m_openDirAct = nullptr;
    QAction *m_quitAct = nullptr;
    QAction *m_zoomInAct = nullptr;
    QAction *m_zoomOutAct = nullptr;
    QAction *m_zoom1to1Act = nullptr;
    QAction *m_zoomFitAct = nullptr;
    QAction *m_fullscreenAct = nullptr;
    QAction *m_rotateLeftAct = nullptr;
    QAction *m_rotateRightAct = nullptr;
    QAction *m_previousAct = nullptr;
    QAction *m_nextAct = nullptr;
    QAction *m_slideshowAct = nullptr;
    QAction *m_layoutSideBySideAct = nullptr;
    QAction *m_clearExtrasAct = nullptr;
    QAction *m_opacityResetAct = nullptr;
    QAction *m_opacityDownAct = nullptr;
    QAction *m_opacityUpAct = nullptr;
    QAction *m_lowerAct = nullptr;
    QAction *m_raiseAct = nullptr;
    QAction *m_sortNameAct = nullptr;
    QAction *m_sortMTimeAct = nullptr;
    QAction *m_toggleToolBarAct = nullptr;
    QAction *m_toggleThumbnailBarAct = nullptr;
    QAction *m_preferencesAct = nullptr;
    QAction *m_aboutAct = nullptr;
    QActionGroup *m_sortGroup = nullptr;

    QStringList m_files;
    int m_currentIndex = -1;
    bool m_recursive = false;
    SortMode m_sortMode = SortMode::Name;
    int m_slideshowIntervalMs = 3000;
    bool m_forceThumbnails = false;
    bool m_forceNoThumbnails = false;
    bool m_slideshowAdvancing = false; // true while timer-driven next runs

    bool m_toolBarVisibleBeforeFullscreen = true;
    bool m_thumbnailBarVisibleBeforeFullscreen = true;
};

#endif // MAINWINDOW_H
