// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"

#include <QAction>
#include <QFileDialog>
#include <QLabel>
#include <QStatusBar>
#include <QToolBar>
#include <QStyle>
#include <QApplication>
#include <QKeySequence>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QImgView"));
    resize(1024, 768);

    m_imageView = new ImageView(this);
    setCentralWidget(m_imageView);

    createActions();
    createToolBar();
    createStatusBar();

    connect(m_imageView, &ImageView::statusChanged, this, &MainWindow::updateStatus);
}

MainWindow::~MainWindow() = default;

void MainWindow::createActions()
{
    m_openAct = new QAction(tr("&Open..."), this);
    m_openAct->setShortcut(QKeySequence::Open);
    m_openAct->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(m_openAct, &QAction::triggered, this, &MainWindow::openFiles);

    m_zoomInAct = new QAction(tr("Zoom &In"), this);
    m_zoomInAct->setShortcut(QKeySequence::ZoomIn);
    m_zoomInAct->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    connect(m_zoomInAct, &QAction::triggered, this, &MainWindow::zoomIn);

    m_zoomOutAct = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    m_zoomOutAct->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    connect(m_zoomOutAct, &QAction::triggered, this, &MainWindow::zoomOut);

    m_zoom1to1Act = new QAction(tr("Zoom &1:1"), this);
    m_zoom1to1Act->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(m_zoom1to1Act, &QAction::triggered, this, &MainWindow::zoomReset);

    m_zoomFitAct = new QAction(tr("&Fit to Window"), this);
    m_zoomFitAct->setShortcut(Qt::CTRL | Qt::Key_F);
    connect(m_zoomFitAct, &QAction::triggered, this, &MainWindow::zoomFit);

    m_fullscreenAct = new QAction(tr("F&ullscreen"), this);
    m_fullscreenAct->setShortcut(Qt::Key_F11);
    m_fullscreenAct->setCheckable(true);
    connect(m_fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    m_rotateLeftAct = new QAction(tr("Rotate &Left"), this);
    m_rotateLeftAct->setShortcut(Qt::CTRL | Qt::Key_L);
    connect(m_rotateLeftAct, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_rotateRightAct = new QAction(tr("Rotate &Right"), this);
    m_rotateRightAct->setShortcut(Qt::CTRL | Qt::Key_R);
    connect(m_rotateRightAct, &QAction::triggered, this, &MainWindow::rotateRight);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(24, 24));

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
        m_fullscreenAct->setChecked(false);
    } else {
        showFullScreen();
        m_fullscreenAct->setChecked(true);
    }
}

void MainWindow::rotateLeft()
{
    m_imageView->rotateLeft();
}

void MainWindow::rotateRight()
{
    m_imageView->rotateRight();
}

void MainWindow::updateStatus()
{
    m_statusLabel->setText(m_imageView->statusText());
}
