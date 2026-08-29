// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"

#include <QAction>
#include <QApplication>
#include <QEvent>
#include <QFileDialog>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>

namespace {

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(fallback);
    }
    return icon;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QImgView"));
    resize(1024, 768);

    m_imageView = new ImageView(this);
    setCentralWidget(m_imageView);
    m_imageView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageView, &QWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    connect(m_imageView, &ImageView::statusChanged, this, &MainWindow::updateStatus);
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    m_openAct = new QAction(tr("&Open..."), this);
    m_openAct->setShortcut(QKeySequence::Open);
    m_openAct->setIcon(themeIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton));
    m_openAct->setStatusTip(tr("Open image files"));
    connect(m_openAct, &QAction::triggered, this, &MainWindow::openFiles);

    m_quitAct = new QAction(tr("&Quit"), this);
    m_quitAct->setShortcut(QKeySequence::Quit);
    m_quitAct->setIcon(themeIcon(QStringLiteral("application-exit"), QStyle::SP_DialogCloseButton));
    m_quitAct->setStatusTip(tr("Quit QImgView"));
    connect(m_quitAct, &QAction::triggered, this, &QWidget::close);

    m_zoomInAct = new QAction(tr("Zoom &In"), this);
    m_zoomInAct->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAct->setIcon(themeIcon(QStringLiteral("zoom-in"), QStyle::SP_ArrowUp));
    m_zoomInAct->setStatusTip(tr("Zoom in"));
    connect(m_zoomInAct, &QAction::triggered, this, &MainWindow::zoomIn);

    m_zoomOutAct = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAct->setIcon(themeIcon(QStringLiteral("zoom-out"), QStyle::SP_ArrowDown));
    m_zoomOutAct->setStatusTip(tr("Zoom out"));
    connect(m_zoomOutAct, &QAction::triggered, this, &MainWindow::zoomOut);

    m_zoom1to1Act = new QAction(tr("Zoom &1:1"), this);
    m_zoom1to1Act->setShortcut(Qt::CTRL | Qt::Key_0);
    m_zoom1to1Act->setIcon(themeIcon(QStringLiteral("zoom-original"), QStyle::SP_DesktopIcon));
    m_zoom1to1Act->setStatusTip(tr("Reset zoom to 100%"));
    connect(m_zoom1to1Act, &QAction::triggered, this, &MainWindow::zoomReset);

    m_zoomFitAct = new QAction(tr("&Fit to Window"), this);
    m_zoomFitAct->setShortcut(Qt::CTRL | Qt::Key_F);
    m_zoomFitAct->setIcon(themeIcon(QStringLiteral("zoom-fit-best"), QStyle::SP_TitleBarMaxButton));
    m_zoomFitAct->setStatusTip(tr("Fit image to the window"));
    connect(m_zoomFitAct, &QAction::triggered, this, &MainWindow::zoomFit);

    m_fullscreenAct = new QAction(tr("F&ullscreen"), this);
    m_fullscreenAct->setShortcut(Qt::Key_F11);
    m_fullscreenAct->setIcon(themeIcon(QStringLiteral("view-fullscreen"), QStyle::SP_TitleBarMaxButton));
    m_fullscreenAct->setCheckable(true);
    m_fullscreenAct->setStatusTip(tr("Toggle fullscreen mode"));
    connect(m_fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    m_rotateLeftAct = new QAction(tr("Rotate &Left"), this);
    m_rotateLeftAct->setShortcut(Qt::CTRL | Qt::Key_L);
    m_rotateLeftAct->setIcon(themeIcon(QStringLiteral("object-rotate-left"), QStyle::SP_ArrowBack));
    m_rotateLeftAct->setStatusTip(tr("Rotate 90° counter-clockwise"));
    connect(m_rotateLeftAct, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_rotateRightAct = new QAction(tr("Rotate &Right"), this);
    m_rotateRightAct->setShortcut(Qt::CTRL | Qt::Key_R);
    m_rotateRightAct->setIcon(themeIcon(QStringLiteral("object-rotate-right"), QStyle::SP_ArrowForward));
    m_rotateRightAct->setStatusTip(tr("Rotate 90° clockwise"));
    connect(m_rotateRightAct, &QAction::triggered, this, &MainWindow::rotateRight);

    m_toggleToolBarAct = new QAction(tr("Show &Toolbar"), this);
    m_toggleToolBarAct->setShortcut(Qt::Key_Tab);
    m_toggleToolBarAct->setCheckable(true);
    m_toggleToolBarAct->setChecked(true);
    m_toggleToolBarAct->setStatusTip(tr("Show or hide the toolbar (Tab)"));
    connect(m_toggleToolBarAct, &QAction::triggered, this, &MainWindow::toggleToolBar);

    m_aboutAct = new QAction(tr("&About QImgView"), this);
    m_aboutAct->setStatusTip(tr("About this application"));
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->addAction(m_openAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_quitAct);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(m_zoomInAct);
    m_viewMenu->addAction(m_zoomOutAct);
    m_viewMenu->addAction(m_zoom1to1Act);
    m_viewMenu->addAction(m_zoomFitAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_rotateLeftAct);
    m_viewMenu->addAction(m_rotateRightAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_fullscreenAct);
    m_viewMenu->addAction(m_toggleToolBarAct);

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addAction(m_aboutAct);

    // Context menu mirrors the most useful actions
    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction(m_openAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_zoomInAct);
    m_contextMenu->addAction(m_zoomOutAct);
    m_contextMenu->addAction(m_zoom1to1Act);
    m_contextMenu->addAction(m_zoomFitAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_rotateLeftAct);
    m_contextMenu->addAction(m_rotateRightAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_fullscreenAct);
    m_contextMenu->addAction(m_toggleToolBarAct);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(24, 24));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_toolBar->addAction(m_openAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_zoomInAct);
    m_toolBar->addAction(m_zoomOutAct);
    m_toolBar->addAction(m_zoom1to1Act);
    m_toolBar->addAction(m_zoomFitAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_rotateLeftAct);
    m_toolBar->addAction(m_rotateRightAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_fullscreenAct);
}

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready"));
    statusBar()->addWidget(m_statusLabel, 1);
}

void MainWindow::loadFiles(const QStringList &files)
{
    if (files.isEmpty()) {
        return;
    }
    // Prototype: load the first file only. Multi-image support comes later.
    m_imageView->loadImage(files.first());
    updateStatus();
}

void MainWindow::openFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Open Images"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp);;All Files (*)"));
    if (!files.isEmpty()) {
        loadFiles(files);
    }
}

void MainWindow::zoomIn()
{
    m_imageView->zoomIn();
}

void MainWindow::zoomOut()
{
    m_imageView->zoomOut();
}

void MainWindow::zoomReset()
{
    m_imageView->zoomReset();
}

void MainWindow::zoomFit()
{
    m_imageView->zoomFit();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
    // changeEvent will call updateFullscreenUi()
}

void MainWindow::rotateLeft()
{
    m_imageView->rotateLeft();
}

void MainWindow::rotateRight()
{
    m_imageView->rotateRight();
}

void MainWindow::toggleToolBar()
{
    const bool visible = m_toggleToolBarAct->isChecked();
    m_toolBar->setVisible(visible);
    if (!isFullScreen()) {
        m_toolBarVisibleBeforeFullscreen = visible;
    }
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("About QImgView"),
        tr("<h3>QImgView %1</h3>"
           "<p>A classic Qt image viewer that treats the image area "
           "as a workspace.</p>"
           "<p>Copyright © 2026 Ingo Ruhnke &lt;grumbel@gmail.com&gt;</p>"
           "<p>License: GPL-3.0-or-later</p>")
            .arg(QApplication::applicationVersion()));
}

void MainWindow::updateStatus()
{
    m_statusLabel->setText(m_imageView->statusText());
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    m_contextMenu->popup(m_imageView->mapToGlobal(pos));
}

}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        updateFullscreenUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::updateFullscreenUi()
{
    const bool fs = isFullScreen();
    m_fullscreenAct->setChecked(fs);

    if (fs) {
        // Remember toolbar state and hide it in fullscreen
        m_toolBarVisibleBeforeFullscreen = m_toolBar->isVisible();
        m_toolBar->setVisible(false);
        m_toggleToolBarAct->setChecked(false);
        // Optionally hide menu bar for a cleaner fullscreen experience
        menuBar()->setVisible(false);
        statusBar()->setVisible(false);
    } else {
        m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
        m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);
        menuBar()->setVisible(true);
        statusBar()->setVisible(true);
    }
}
