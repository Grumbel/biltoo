// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QEvent>
#include <QStringList>

class ImageView;
class QToolBar;
class QAction;
class QLabel;
class QMenu;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadFiles(const QStringList &files);

protected:
    void changeEvent(QEvent *event) override;

private slots:
    void openFiles();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void toggleFullscreen();
    void rotateLeft();
    void rotateRight();
    void toggleToolBar();
    void about();
    void updateStatus();
    void showContextMenu(const QPoint &pos);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createStatusBar();
    void updateFullscreenUi();

    ImageView *m_imageView = nullptr;
    QToolBar *m_toolBar = nullptr;
    QLabel *m_statusLabel = nullptr;

    QMenu *m_fileMenu = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_helpMenu = nullptr;
    QMenu *m_contextMenu = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_quitAct = nullptr;
    QAction *m_zoomInAct = nullptr;
    QAction *m_zoomOutAct = nullptr;
    QAction *m_zoom1to1Act = nullptr;
    QAction *m_zoomFitAct = nullptr;
    QAction *m_fullscreenAct = nullptr;
    QAction *m_rotateLeftAct = nullptr;
    QAction *m_rotateRightAct = nullptr;
    QAction *m_toggleToolBarAct = nullptr;
    QAction *m_aboutAct = nullptr;

    bool m_toolBarVisibleBeforeFullscreen = true;
};

#endif // MAINWINDOW_H
