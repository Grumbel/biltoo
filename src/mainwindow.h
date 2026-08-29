// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStringList>

class ImageView;
class QToolBar;
class QAction;
class QLabel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadFiles(const QStringList &files);

private slots:
    void openFiles();
    void zoomIn();
    void zoomOut();
    void zoomReset();
    void zoomFit();
    void toggleFullscreen();
    void rotateLeft();
    void rotateRight();
    void updateStatus();

private:
    void createActions();
    void createToolBar();
    void createStatusBar();

    ImageView *m_imageView = nullptr;
    QToolBar *m_toolBar = nullptr;
    QLabel *m_statusLabel = nullptr;

    QAction *m_openAct = nullptr;
    QAction *m_zoomInAct = nullptr;
    QAction *m_zoomOutAct = nullptr;
    QAction *m_zoom1to1Act = nullptr;
    QAction *m_zoomFitAct = nullptr;
    QAction *m_fullscreenAct = nullptr;
    QAction *m_rotateLeftAct = nullptr;
    QAction *m_rotateRightAct = nullptr;
};

#endif // MAINWINDOW_H
