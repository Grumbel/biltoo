// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "icons.h"
#include "imageview.h"
#include "imageloader.h"
#include "thumbnailbar.h"
#include "preferencesdialog.h"
#include "metadatapanel.h"

#include <QAbstractButton>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCollator>
#include <QDir>
#include <QDockWidget>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QShortcut>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QSizePolicy>
#include <QSet>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QSplitter>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QImgView"));
    setWindowIcon(QApplication::windowIcon());
    resize(1024, 768);
    setAcceptDrops(true);

    m_centralSplitter = new QSplitter(Qt::Vertical, this);
    m_centralSplitter->setObjectName(QStringLiteral("CentralSplitter"));
    m_centralSplitter->setChildrenCollapsible(false);
    m_centralSplitter->setHandleWidth(6);

    m_imageView = new ImageView(m_centralSplitter);
    m_imageView->setAccessibleName(tr("Image view"));
    m_imageView->setAccessibleDescription(
        tr("Shows the current image. In image mode, click the left or right edge "
           "to go to the previous or next image. Use Go menu for keyboard navigation."));
    m_imageView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_imageView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageView, &QWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);
    connect(m_imageView, &ImageView::mouseInfoChanged,
            this, &MainWindow::onMouseInfoChanged);
    connect(m_imageView, &ImageView::navigatePreviousRequested,
            this, &MainWindow::goPrevious);
    connect(m_imageView, &ImageView::navigateNextRequested,
            this, &MainWindow::goNext);
    connect(m_imageView, &ImageView::fullscreenToggleRequested,
            this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageView::galleryItemOpenRequested,
            this, &MainWindow::openGalleryItemInImageMode);
    connect(m_imageView, &ImageView::filesDropped,
            this, &MainWindow::onFilesDropped);

    m_thumbnailBar = new ThumbnailBar(m_centralSplitter);
    m_thumbnailBar->setAccessibleName(tr("Thumbnails"));
    connect(m_thumbnailBar, &ThumbnailBar::indexActivated,
            this, &MainWindow::onThumbnailActivated);
    connect(m_thumbnailBar, &ThumbnailBar::indexAddToWorkspace,
            this, &MainWindow::onThumbnailAddToWorkspace);
    connect(m_thumbnailBar, &ThumbnailBar::workspaceSelectionChanged,
            this, &MainWindow::onThumbnailWorkspaceSelectionChanged);
    connect(m_thumbnailBar, &ThumbnailBar::removeIndicesRequested,
            this, &MainWindow::removeSessionIndices);
    connect(m_imageView, &ImageView::workspacePathsChanged,
            this, &MainWindow::onWorkspacePathsChanged);

    m_centralSplitter->addWidget(m_imageView);
    m_centralSplitter->addWidget(m_thumbnailBar);
    m_centralSplitter->setStretchFactor(0, 1);
    m_centralSplitter->setStretchFactor(1, 0);
    m_imageView->setMinimumHeight(120);
    setCentralWidget(m_centralSplitter);

    m_metadataPanel = new MetadataPanel(this);
    m_metadataDock = new QDockWidget(tr("Metadata"), this);
    m_metadataDock->setObjectName(QStringLiteral("MetadataDock"));
    m_metadataDock->setWidget(m_metadataPanel);
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_metadataDock->setFeatures(QDockWidget::DockWidgetClosable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
    m_metadataDock->hide();

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

    // Use status tips as hover tooltips on the toolbar and menus
    for (QAction *act : findChildren<QAction *>()) {
        if (act->toolTip().isEmpty() && !act->statusTip().isEmpty()) {
            act->setToolTip(act->statusTip());
        }
    }

    // Application-wide shortcuts so they work while the image view has focus
    auto *escShortcut = new QShortcut(Qt::Key_Escape, this);
    escShortcut->setContext(Qt::ApplicationShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        if (isFullScreen()) {
            showNormal();
            return;
        }
        if (m_galleryReturnActive && m_imageView && m_imageView->isImageMode()) {
            returnToGallery();
        }
    });

    connect(m_imageView, &ImageView::statusChanged, this, &MainWindow::updateStatus);

    m_slideshowTimer = new QTimer(this);
    m_slideshowTimer->setTimerType(Qt::PreciseTimer);
    connect(m_slideshowTimer, &QTimer::timeout, this, &MainWindow::onSlideshowTick);

    m_cursorHideTimer = new QTimer(this);
    m_cursorHideTimer->setSingleShot(true);
    m_cursorHideTimer->setInterval(1000);
    connect(m_cursorHideTimer, &QTimer::timeout, this, &MainWindow::hideSlideshowCursor);

    m_thumbnailBar->setVisible(false);
    updateNavigationActions();
    readSettings();
    updateWorkspaceActionVisibility();
}

MainWindow::~MainWindow() = default;

void MainWindow::onThumbnailAddToWorkspace(int index)
{
    if (!m_workspaceMode) {
        // Enable workspace mode automatically when the user explicitly adds
        m_workspaceMode = true;
        m_workspaceModeAct->setChecked(true);
        m_imageView->setWorkspaceMode(true);
        m_thumbnailBar->setWorkspaceMode(true);
        updateWorkspaceActionVisibility();
        // Seed selection with the image being added
        if (index >= 0 && index < m_files.size()) {
            m_thumbnailBar->setSelectedIndices({index});
        }
    }
    if (index < 0 || index >= m_files.size()) {
        return;
    }
    m_imageView->addImage(m_files.at(index));
    syncThumbnailWorkspaceSelection();
}

void MainWindow::onThumbnailWorkspaceSelectionChanged()
{
    if (!m_workspaceMode) {
        return;
    }
    applyWorkspaceSelectionFromThumbnails();
}

void MainWindow::onWorkspacePathsChanged()
{
    if (!m_workspaceMode) {
        return;
    }
    syncThumbnailWorkspaceSelection();
}

void MainWindow::applyWorkspaceSelectionFromThumbnails()
{
    QStringList paths;
    for (int idx : m_thumbnailBar->selectedIndices()) {
        if (idx >= 0 && idx < m_files.size()) {
            paths.append(m_files.at(idx));
        }
    }
    m_imageView->setWorkspacePaths(paths);

    // Keep session index / metadata in sync with last selected thumbnail
    const QList<int> sel = m_thumbnailBar->selectedIndices();
    if (!sel.isEmpty()) {
        const int idx = sel.last();
        if (idx != m_currentIndex && idx >= 0 && idx < m_files.size()) {
            m_currentIndex = idx;
            if (m_metadataPanel) {
                m_metadataPanel->setImagePath(m_files.at(m_currentIndex));
            }
        }
    }
    updateStatus();
}

void MainWindow::syncThumbnailWorkspaceSelection()
{
    if (!m_workspaceMode) {
        return;
    }
    QList<int> indices;
    for (const QString &path : m_imageView->itemPaths()) {
        const int idx = m_files.indexOf(path);
        if (idx >= 0) {
            indices.append(idx);
        }
    }
    m_thumbnailBar->setSelectedIndices(indices);
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

void MainWindow::zoomFill()
{
    m_imageView->zoomFill();
}

void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
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

void MainWindow::flipHorizontal()
{
    m_imageView->flipHorizontal();
}

void MainWindow::flipVertical()
{
    m_imageView->flipVertical();
}

void MainWindow::toggleHud()
{
    const bool on = m_toggleHudAct->isChecked();
    m_imageView->setHudVisible(on);
}

void MainWindow::toggleThumbnailLabels()
{
    if (!m_thumbnailBar) {
        return;
    }
    m_thumbnailBar->setLabelsVisible(!m_hideThumbLabelsAct->isChecked());
}

void MainWindow::ensureMultiImageMode()
{
    if (m_workspaceMode) {
        return;
    }
    m_workspaceMode = true;
    m_workspaceModeAct->setChecked(true);
    m_imageView->setWorkspaceMode(true);
    m_thumbnailBar->setWorkspaceMode(true);
    if (m_imageView->itemCount() == 0
        && m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
        m_imageView->addImage(m_files.at(m_currentIndex));
    }
    syncThumbnailWorkspaceSelection();
    if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
        m_toggleThumbnailBarAct->setChecked(true);
        m_thumbnailBar->setVisible(true);
    }
    updateWorkspaceActionVisibility();
}

void MainWindow::raiseSelected()
{
    m_imageView->raiseSelected();
}

void MainWindow::lowerSelected()
{
    m_imageView->lowerSelected();
}

void MainWindow::opacityUp()
{
    m_imageView->opacityUp();
}

void MainWindow::opacityDown()
{
    m_imageView->opacityDown();
}

void MainWindow::opacityReset()
{
    m_imageView->opacityReset();
}

void MainWindow::clearWorkspaceExtras()
{
    m_imageView->clearExtras();
}

void MainWindow::toggleWorkspaceMode()
{
    const bool on = m_workspaceModeAct->isChecked();
    if (on) {
        m_galleryReturnActive = false;
        if (m_backToGalleryAct) {
            m_backToGalleryAct->setEnabled(false);
        }
        m_workspaceMode = true;
        m_thumbnailBar->setWorkspaceMode(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        if (m_imageView->itemCount() == 0
            && m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            m_imageView->addImage(m_files.at(m_currentIndex));
        }
        syncThumbnailWorkspaceSelection();
        if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
        // Uncheck gallery layout actions
        for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                             m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutStackAct}) {
            if (act) {
                act->setChecked(false);
            }
        }
    } else {
        m_workspaceMode = false;
        m_thumbnailBar->setWorkspaceMode(false);
        m_imageView->setViewMode(ImageView::ViewMode::Image);
        if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            m_imageView->loadImage(m_files.at(m_currentIndex));
            m_thumbnailBar->setCurrentIndex(m_currentIndex);
        }
    }
    updateWorkspaceActionVisibility();
}

void MainWindow::setSelectTool()
{
    m_imageView->setTool(ImageView::Tool::Select);
    m_selectToolAct->setChecked(true);
}

void MainWindow::setPanTool()
{
    m_imageView->setTool(ImageView::Tool::Pan);
    m_panToolAct->setChecked(true);
}


void MainWindow::onSlideshowTick()
{
    m_slideshowAdvancing = true;
    goNext();
    m_slideshowAdvancing = false;
}

void MainWindow::toggleToolBar()
{
    const bool visible = m_toggleToolBarAct->isChecked();
    m_toolBar->setVisible(visible);
    if (!isFullScreen()) {
        m_toolBarVisibleBeforeFullscreen = visible;
    }
}

void MainWindow::toggleThumbnailBar()
{
    const bool visible = m_toggleThumbnailBarAct->isChecked();
    m_thumbnailBar->setVisible(visible);
    m_forceNoThumbnails = !visible;
    m_forceThumbnails = visible;
    if (!isFullScreen()) {
        m_thumbnailBarVisibleBeforeFullscreen = visible;
    }
}

void MainWindow::setThumbnailBarPosition(ThumbnailEdge edge)
{
    m_thumbnailEdge = edge;
    const bool horizontalBar =
        (edge == ThumbnailEdge::Bottom || edge == ThumbnailEdge::Top);
    const bool barFirst =
        (edge == ThumbnailEdge::Top || edge == ThumbnailEdge::Left);

    const Qt::Orientation barOrientation =
        horizontalBar ? Qt::Horizontal : Qt::Vertical;
    const Qt::Orientation splitOrientation =
        horizontalBar ? Qt::Vertical : Qt::Horizontal;

    if (m_thumbnailBar->barOrientation() == barOrientation
        && m_centralSplitter->orientation() == splitOrientation) {
        // May still need to swap widget order (top vs bottom, left vs right)
        const bool imageFirst = (m_centralSplitter->widget(0) == m_imageView);
        if ((barFirst && !imageFirst) || (!barFirst && imageFirst)) {
            // already correct order
            updateThumbnailEdgeActions();
            return;
        }
    }

    const int thumbSize = m_thumbnailBar->thumbSize();
    const bool barVisible = m_thumbnailBar->isVisible();

    m_thumbnailBar->setBarOrientation(barOrientation);

    m_imageView->setParent(nullptr);
    m_thumbnailBar->setParent(nullptr);
    while (m_centralSplitter->count() > 0) {
        m_centralSplitter->widget(0)->setParent(nullptr);
    }

    m_centralSplitter->setOrientation(splitOrientation);
    if (barFirst) {
        m_centralSplitter->addWidget(m_thumbnailBar);
        m_centralSplitter->addWidget(m_imageView);
        m_centralSplitter->setStretchFactor(0, 0);
        m_centralSplitter->setStretchFactor(1, 1);
    } else {
        m_centralSplitter->addWidget(m_imageView);
        m_centralSplitter->addWidget(m_thumbnailBar);
        m_centralSplitter->setStretchFactor(0, 1);
        m_centralSplitter->setStretchFactor(1, 0);
    }

    const int barExtent = ThumbnailBar::extentForThumbSize(thumbSize);
    if (horizontalBar) {
        m_imageView->setMinimumHeight(120);
        m_imageView->setMinimumWidth(0);
        const int img = qMax(200, height() - barExtent - 80);
        if (barFirst) {
            m_centralSplitter->setSizes({barExtent, img});
        } else {
            m_centralSplitter->setSizes({img, barExtent});
        }
    } else {
        m_imageView->setMinimumWidth(120);
        m_imageView->setMinimumHeight(0);
        const int img = qMax(200, width() - barExtent - 40);
        if (barFirst) {
            m_centralSplitter->setSizes({barExtent, img});
        } else {
            m_centralSplitter->setSizes({img, barExtent});
        }
    }

    m_thumbnailBar->setThumbSize(thumbSize);
    m_thumbnailBar->setVisible(barVisible);
    updateThumbnailEdgeActions();
}

void MainWindow::updateThumbnailEdgeActions()
{
    if (m_thumbnailsBottomAct) {
        m_thumbnailsBottomAct->setChecked(m_thumbnailEdge == ThumbnailEdge::Bottom);
    }
    if (m_thumbnailsTopAct) {
        m_thumbnailsTopAct->setChecked(m_thumbnailEdge == ThumbnailEdge::Top);
    }
    if (m_thumbnailsLeftAct) {
        m_thumbnailsLeftAct->setChecked(m_thumbnailEdge == ThumbnailEdge::Left);
    }
    if (m_thumbnailsRightAct) {
        m_thumbnailsRightAct->setChecked(m_thumbnailEdge == ThumbnailEdge::Right);
    }
}


void MainWindow::toggleScrollBars()
{
    const bool show = m_toggleScrollBarsAct->isChecked();
    const auto policy = show ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff;
    m_imageView->setHorizontalScrollBarPolicy(policy);
    m_imageView->setVerticalScrollBarPolicy(policy);
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About QImgView"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    box.setText(tr("<h3>QImgView %1</h3>").arg(QApplication::applicationVersion()));
    box.setInformativeText(
        tr("<p>A classic image viewer with an optional workspace for "
           "comparing images side by side.</p>"
           "<p>Copyright © 2026 Ingo Ruhnke &lt;grumbel@gmail.com&gt;<br/>"
           "License: <b>GPL-3.0-or-later</b></p>"
           "<p>Shortcuts: <b>F</b>/<b>F11</b> fullscreen, <b>Esc</b> leave fullscreen, "
           "<b>Ctrl+T</b> toolbar, <b>Space</b> slideshow.</p>"));
    // GNOME 2 HIG: single affirmative Close on the right is fine for about boxes
    box.setStandardButtons(QMessageBox::Close);
    box.button(QMessageBox::Close)->setText(tr("&Close"));
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
}

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    dlg.setSlideshowIntervalMs(m_slideshowIntervalMs);
    dlg.setSlideshowFullscreen(m_slideshowFullscreen);
    dlg.setSortModeIndex(m_sortMode == SortMode::Name ? 0 : 1);
    dlg.setStartInWorkspaceMode(m_startInWorkspaceMode);
    dlg.setImageModeLeftDragPan(m_imageView->imageModeLeftDragPan());
    dlg.setBackgroundColor(m_imageView->backgroundColor());
    dlg.setBackgroundColorAlt(m_imageView->backgroundColorAlt());
    dlg.setBackgroundPatternIndex(
        m_imageView->backgroundPattern() == ImageView::BackgroundPattern::Checkerboard ? 1 : 0);
    dlg.setCheckerboardWorkspaceOnly(m_imageView->checkerboardWorkspaceOnly());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    setSlideshowIntervalMs(dlg.slideshowIntervalMs());
    m_slideshowFullscreen = dlg.slideshowFullscreen();
    setSortMode(dlg.sortModeIndex() == 1 ? SortMode::MTime : SortMode::Name);
    m_startInWorkspaceMode = dlg.startInWorkspaceMode();
    m_imageView->setImageModeLeftDragPan(dlg.imageModeLeftDragPan());
    m_imageView->setBackgroundColor(dlg.backgroundColor());
    m_imageView->setBackgroundColorAlt(dlg.backgroundColorAlt());
    m_imageView->setBackgroundPattern(
        dlg.backgroundPatternIndex() == 1 ? ImageView::BackgroundPattern::Checkerboard
                                          : ImageView::BackgroundPattern::Solid);
    m_imageView->setCheckerboardWorkspaceOnly(dlg.checkerboardWorkspaceOnly());
    writeSettings();
}

void MainWindow::selectAllThumbnails()
{
    if (!m_thumbnailBar || m_files.isEmpty()) {
        return;
    }
    if (!m_workspaceMode) {
        m_workspaceMode = true;
        if (m_workspaceModeAct) {
            m_workspaceModeAct->setChecked(true);
        }
        if (m_imageView) {
            m_imageView->setWorkspaceMode(true);
        }
        m_thumbnailBar->setWorkspaceMode(true);
        updateWorkspaceActionVisibility();
    }
    m_thumbnailBar->selectAllThumbs();
}

void MainWindow::flashNavHud(const QString &action)
{
    if (!m_imageView || m_workspaceMode) {
        return;
    }
    QString detail;
    if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
        detail = QFileInfo(m_files.at(m_currentIndex)).fileName();
        if (m_files.size() > 1) {
            detail += tr("  (%1/%2)").arg(m_currentIndex + 1).arg(m_files.size());
        }
    }
    m_imageView->flashHud(action, detail);
}

void MainWindow::updateWindowTitle()
{
    if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
        const QString name = QFileInfo(m_files.at(m_currentIndex)).fileName();
        // GNOME-style: document name first, then app
        setWindowTitle(tr("%1 — QImgView").arg(name));
    } else {
        setWindowTitle(tr("QImgView"));
    }
}

void MainWindow::updateStatus()
{
    updateNavigationActions();
    QString text = m_imageView->statusText();
    if (m_files.size() > 1 && m_currentIndex >= 0) {
        text = tr("[%1/%2]  %3")
                   .arg(m_currentIndex + 1)
                   .arg(m_files.size())
                   .arg(text);
    }
    m_statusLabel->setText(text);
}

void MainWindow::onMouseInfoChanged(const ImageMouseInfo &info)
{
    if (!info.valid) {
        m_mouseLabel->clear();
        if (m_colorSwatch) {
            m_colorSwatch->setStyleSheet(QStringLiteral(
                "QLabel { border: 1px solid #888; background: transparent; }"));
            m_colorSwatch->setToolTip(tr("Colour under the cursor"));
        }
        return;
    }
    const QColor &c = info.pixelColor;
    m_mouseLabel->setText(
        tr("(%1, %2)  RGB %3 %4 %5")
            .arg(info.imagePos.x())
            .arg(info.imagePos.y())
            .arg(c.red())
            .arg(c.green())
            .arg(c.blue()));
    if (m_colorSwatch) {
        m_colorSwatch->setStyleSheet(
            QStringLiteral("QLabel { border: 1px solid #888; background-color: %1; }")
                .arg(c.name()));
        m_colorSwatch->setToolTip(
            tr("RGB %1 %2 %3 (%4)")
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue())
                .arg(c.name()));
    }
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    m_contextMenu->popup(m_imageView->mapToGlobal(pos));
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

    // Leaving fullscreen stops an active slideshow (Space/F5 start; Esc stops)
    if (!fs && m_slideshowTimer && m_slideshowTimer->isActive()) {
        stopSlideshow();
    }

    if (fs) {
        m_toolBarVisibleBeforeFullscreen = m_toolBar->isVisible();
        m_thumbnailBarVisibleBeforeFullscreen = m_thumbnailBar->isVisible();
        m_metadataVisibleBeforeFullscreen =
            m_metadataDock && m_metadataDock->isVisible();
        m_toolBar->setVisible(false);
        if (m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(false);
        }
        m_thumbnailBar->setVisible(false);
        m_metadataDock->setVisible(false);
        m_toggleToolBarAct->setChecked(false);
        m_toggleThumbnailBarAct->setChecked(false);
        m_toggleMetadataAct->setChecked(false);
        menuBar()->setVisible(false);
        statusBar()->setVisible(false);
    } else {
        m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
        m_thumbnailBar->setVisible(m_thumbnailBarVisibleBeforeFullscreen);
        m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);
        m_toggleThumbnailBarAct->setChecked(m_thumbnailBarVisibleBeforeFullscreen);
        if (m_metadataDock) {
            m_metadataDock->setVisible(m_metadataVisibleBeforeFullscreen);
        }
        if (m_toggleMetadataAct) {
            m_toggleMetadataAct->setChecked(m_metadataVisibleBeforeFullscreen);
        }
        if (m_workspaceMode && m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(true);
        }
        menuBar()->setVisible(true);
        statusBar()->setVisible(true);
    }
}

void MainWindow::readSettings()
{
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray state = settings.value(QStringLiteral("windowState")).toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    m_toolBarVisibleBeforeFullscreen =
        settings.value(QStringLiteral("toolBarVisible"), true).toBool();
    m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
    m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);

    const QString sort = settings.value(QStringLiteral("sortMode"), QStringLiteral("name")).toString();
    if (sort == QLatin1String("mtime")) {
        m_sortMode = SortMode::MTime;
        m_sortMTimeAct->setChecked(true);
    } else {
        m_sortMode = SortMode::Name;
        m_sortNameAct->setChecked(true);
    }

    m_slideshowIntervalMs =
        settings.value(QStringLiteral("slideshowIntervalMs"), 3000).toInt();
    const int masonryCols = settings.value(QStringLiteral("masonryColumns"), 3).toInt();
    const int masonryRows = settings.value(QStringLiteral("masonryRows"), 3).toInt();
    if (m_imageView) {
        m_imageView->setMasonryColumns(masonryCols);
        m_imageView->setMasonryRows(masonryRows);
    }
    if (m_masonryCountSpin) {
        const QSignalBlocker blocker(m_masonryCountSpin);
        m_masonryCountSpin->setValue(m_imageView ? m_imageView->masonryColumns()
                                                 : masonryCols);
    }
    m_slideshowFullscreen =
        settings.value(QStringLiteral("slideshowFullscreen"), true).toBool();

    // Workspace mode is off by default. Only enable at startup when the user
    // opted in via Preferences ("Start in workspace mode").
    m_startInWorkspaceMode =
        settings.value(QStringLiteral("startInWorkspaceMode"), false).toBool();
    // Migrate legacy key if present and new key never set
    if (!settings.contains(QStringLiteral("startInWorkspaceMode"))
        && settings.contains(QStringLiteral("workspaceMode"))) {
        // Do not migrate "true" from a previous session toggle — always prefer off
        settings.remove(QStringLiteral("workspaceMode"));
        m_startInWorkspaceMode = false;
    }
    m_workspaceMode = m_startInWorkspaceMode;
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(m_workspaceMode);
    }
    if (m_imageView) {
        m_imageView->setWorkspaceMode(m_workspaceMode);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setWorkspaceMode(m_workspaceMode);
        const int thumbSize = settings.value(QStringLiteral("thumbnailSize"),
                                             ThumbnailBar::kDefaultThumbSize).toInt();
        m_thumbnailBar->setThumbSize(thumbSize);
        const QString pos = settings.value(QStringLiteral("thumbnailBarPosition"),
                                           QStringLiteral("bottom")).toString();
        ThumbnailEdge edge = ThumbnailEdge::Bottom;
        if (pos == QLatin1String("left")) {
            edge = ThumbnailEdge::Left;
        } else if (pos == QLatin1String("right")) {
            edge = ThumbnailEdge::Right;
        } else if (pos == QLatin1String("top")) {
            edge = ThumbnailEdge::Top;
        }
        setThumbnailBarPosition(edge);
    }
    if (m_centralSplitter) {
        const QByteArray splitterState =
            settings.value(QStringLiteral("centralSplitter")).toByteArray();
        if (!splitterState.isEmpty()) {
            m_centralSplitter->restoreState(splitterState);
        } else if (m_thumbnailBar) {
            const int extent = ThumbnailBar::extentForThumbSize(m_thumbnailBar->thumbSize());
            if (m_thumbnailBar->barOrientation() == Qt::Horizontal) {
                m_centralSplitter->setSizes({qMax(200, height() - extent - 80), extent});
            } else {
                m_centralSplitter->setSizes({extent, qMax(200, width() - extent - 40)});
            }
        }
    }
    updateWorkspaceActionVisibility();

    const bool showBars = settings.value(QStringLiteral("scrollBarsVisible"), false).toBool();
    if (m_toggleScrollBarsAct) {
        m_toggleScrollBarsAct->setChecked(showBars);
        toggleScrollBars();
    }

    if (m_imageView) {
        const bool leftPan =
            settings.value(QStringLiteral("imageModeLeftDragPan"), true).toBool();
        m_imageView->setImageModeLeftDragPan(leftPan);
        const bool hud = settings.value(QStringLiteral("hudVisible"), false).toBool();
        m_imageView->setHudVisible(hud);
        if (m_toggleHudAct) {
            m_toggleHudAct->setChecked(hud);
        }
        const QColor bg = QColor(settings.value(QStringLiteral("backgroundColor"),
                                                QStringLiteral("#2a2a2a")).toString());
        if (bg.isValid()) {
            m_imageView->setBackgroundColor(bg);
        }
        const QColor bgAlt = QColor(settings.value(QStringLiteral("backgroundColorAlt"),
                                                   QStringLiteral("#303030")).toString());
        if (bgAlt.isValid()) {
            m_imageView->setBackgroundColorAlt(bgAlt);
        }
        const QString pat = settings.value(QStringLiteral("backgroundPattern"),
                                           QStringLiteral("checkerboard")).toString();
        m_imageView->setBackgroundPattern(
            pat == QLatin1String("solid") ? ImageView::BackgroundPattern::Solid
                                          : ImageView::BackgroundPattern::Checkerboard);
        m_imageView->setCheckerboardWorkspaceOnly(
            settings.value(QStringLiteral("checkerboardWorkspaceOnly"), true).toBool());
    }
    if (m_thumbnailBar) {
        const bool labels =
            settings.value(QStringLiteral("thumbnailLabelsVisible"), true).toBool();
        m_thumbnailBar->setLabelsVisible(labels);
        if (m_hideThumbLabelsAct) {
            m_hideThumbLabelsAct->setChecked(!labels);
        }
    }
}

void MainWindow::writeSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("windowState"), saveState());
    settings.setValue(QStringLiteral("toolBarVisible"),
                      isFullScreen() ? m_toolBarVisibleBeforeFullscreen
                                     : m_toolBar->isVisible());
    settings.setValue(QStringLiteral("sortMode"),
                      m_sortMode == SortMode::MTime ? QStringLiteral("mtime")
                                                    : QStringLiteral("name"));
    settings.setValue(QStringLiteral("slideshowIntervalMs"), m_slideshowIntervalMs);
    settings.setValue(QStringLiteral("slideshowFullscreen"), m_slideshowFullscreen);
    if (m_imageView) {
        settings.setValue(QStringLiteral("masonryColumns"),
                          m_imageView->masonryColumns());
        settings.setValue(QStringLiteral("masonryRows"),
                          m_imageView->masonryRows());
        settings.setValue(QStringLiteral("backgroundColor"),
                          m_imageView->backgroundColor().name(QColor::HexRgb));
        settings.setValue(QStringLiteral("backgroundColorAlt"),
                          m_imageView->backgroundColorAlt().name(QColor::HexRgb));
        settings.setValue(QStringLiteral("backgroundPattern"),
                          m_imageView->backgroundPattern()
                                  == ImageView::BackgroundPattern::Solid
                              ? QStringLiteral("solid")
                              : QStringLiteral("checkerboard"));
        settings.setValue(QStringLiteral("checkerboardWorkspaceOnly"),
                          m_imageView->checkerboardWorkspaceOnly());
    }
    // Persist startup preference only — not the live session toggle
    settings.setValue(QStringLiteral("startInWorkspaceMode"), m_startInWorkspaceMode);
    settings.remove(QStringLiteral("workspaceMode"));
    settings.setValue(QStringLiteral("scrollBarsVisible"),
                      m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked());
    if (m_thumbnailBar) {
        settings.setValue(QStringLiteral("thumbnailSize"), m_thumbnailBar->thumbSize());
        settings.setValue(QStringLiteral("thumbnailBarPosition"),
                          m_thumbnailEdge == ThumbnailEdge::Left
                              ? QStringLiteral("left")
                              : m_thumbnailEdge == ThumbnailEdge::Right
                                    ? QStringLiteral("right")
                                    : m_thumbnailEdge == ThumbnailEdge::Top
                                          ? QStringLiteral("top")
                                          : QStringLiteral("bottom"));
    }
    if (m_centralSplitter) {
        settings.setValue(QStringLiteral("centralSplitter"), m_centralSplitter->saveState());
    }
    if (m_imageView) {
        settings.setValue(QStringLiteral("imageModeLeftDragPan"),
                          m_imageView->imageModeLeftDragPan());
        settings.setValue(QStringLiteral("hudVisible"), m_imageView->hudVisible());
    }
    if (m_thumbnailBar) {
        settings.setValue(QStringLiteral("thumbnailLabelsVisible"),
                          m_thumbnailBar->labelsVisible());
    }
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (isFullScreen()) {
            showNormal();
            event->accept();
            return;
        }
        if (m_galleryReturnActive && m_imageView && m_imageView->isImageMode()) {
            returnToGallery();
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    writeSettings();
    QMainWindow::closeEvent(event);
}

QStringList MainWindow::extractLocalImagePaths(const QMimeData *mime) const
{
    QStringList result;
    if (!mime || !mime->hasUrls()) {
        return result;
    }
    for (const QUrl &url : mime->urls()) {
        if (url.isLocalFile()) {
            result.append(url.toLocalFile());
        }
    }
    return result;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!extractLocalImagePaths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::handleDroppedUrls(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                   const QPointF &scenePos)
{
    // Build a transient mime payload so extractLocalImagePaths stays the single filter
    QMimeData mime;
    mime.setUrls(urls);
    const QStringList paths = extractLocalImagePaths(&mime);
    if (paths.isEmpty()) {
        return;
    }

    // Workspace mode: append to the session (thumbnail bar) and show on canvas
    if (m_workspaceMode) {
        const QStringList expanded = expandPaths(paths);
        if (expanded.isEmpty()) {
            return;
        }
        appendFiles(expanded);
        int i = 0;
        for (const QString &img : expanded) {
            if (!scenePos.isNull()) {
                // Cascade slightly so multi-file drops stay visible
                const QPointF pos = scenePos + QPointF(28.0 * i, 22.0 * i);
                m_imageView->addImageAt(img, pos);
            } else {
                m_imageView->addImage(img); // empty-space placement
            }
            ++i;
        }
        syncThumbnailWorkspaceSelection();
        if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
        return;
    }

    // Image mode: Shift or Ctrl+drop appends; plain drop replaces
    if (modifiers & (Qt::ShiftModifier | Qt::ControlModifier)) {
        appendFiles(paths);
    } else {
        loadFiles(paths);
    }
}

void MainWindow::onFilesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                const QPointF &scenePos)
{
    handleDroppedUrls(urls, modifiers, scenePos);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        return;
    }
    // Window-level drop has no reliable scene position — empty-space placement
    handleDroppedUrls(event->mimeData()->urls(), event->modifiers(), QPointF());
    event->acceptProposedAction();
}
