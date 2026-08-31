// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"

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
    connect(m_imageView, &ImageView::galleryReturnRequested,
            this, &MainWindow::returnToGallery);
    connect(m_imageView, &ImageView::fullscreenToggleRequested,
            this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageView::galleryItemOpenRequested,
            this, &MainWindow::openGalleryItemInImageMode);
    connect(m_imageView, &ImageView::sessionRemovePathsRequested,
            this, &MainWindow::removeSessionPaths);
    connect(m_imageView, &ImageView::galleryItemFocused,
            this, [this](const QString &path) {
                const int idx = m_files.indexOf(path);
                if (idx >= 0) {
                    setCurrentIndex(idx);
                }
            });
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
    connect(m_thumbnailBar, &ThumbnailBar::canvasMembershipToggled,
            this, &MainWindow::onThumbnailCanvasMembershipToggled);
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

    bindViewerShortcuts();

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

    // Dedicated F/F11 shortcuts (ApplicationShortcut) so leave-fullscreen is
    // reliable even when the checkable action and window state briefly disagree.
    // The action keeps the same keys for menus/tooltips; Qt de-duplicates.
    for (const int key : {static_cast<int>(Qt::Key_F), static_cast<int>(Qt::Key_F11)}) {
        auto *sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::ApplicationShortcut);
        connect(sc, &QShortcut::activated, this, [this]() {
            if (isFullScreen()) {
                showNormal();
            } else {
                showFullScreen();
            }
        });
    }

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

bool MainWindow::isWorkspaceMode() const
{
    return m_imageView && m_imageView->isWorkspaceMode();
}

bool MainWindow::isGalleryMode() const
{
    return m_imageView && m_imageView->isGalleryMode();
}

bool MainWindow::isImageMode() const
{
    return m_imageView && m_imageView->isImageMode();
}

void MainWindow::onThumbnailAddToWorkspace(int index)
{
    if (!isWorkspaceMode()) {
        // Enter Workspace when the user explicitly adds from the strip
        m_workspaceModeAct->setChecked(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        m_thumbnailBar->setMultiSelectEnabled(true);
        updateWorkspaceActionVisibility();
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
    // Selection is independent of canvas membership. Only keep session
    // index / metadata aligned with the last selected thumbnail.
    if (!isWorkspaceMode()) {
        return;
    }
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

void MainWindow::onThumbnailCanvasMembershipToggled(int index)
{
    if (index < 0 || index >= m_files.size() || !m_imageView) {
        return;
    }
    if (!isWorkspaceMode()) {
        m_workspaceModeAct->setChecked(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        m_thumbnailBar->setMultiSelectEnabled(true);
        updateWorkspaceActionVisibility();
    }
    const QString path = m_files.at(index);
    if (m_imageView->itemPaths().contains(path)) {
        m_imageView->removeWorkspacePath(path);
    } else {
        m_imageView->addImage(path);
    }
    updateStatus();
}

void MainWindow::onWorkspacePathsChanged()
{
    // Canvas membership no longer mirrors thumbnail selection.
    if (!isWorkspaceMode()) {
        return;
    }
    updateStatus();
}

void MainWindow::syncCanvasFromThumbnailSelection()
{
    // Kept for callers that still expect a bulk "selection → canvas" path
    // (e.g. future context-menu actions). Not used for ordinary clicks.
    QStringList paths;
    for (int idx : m_thumbnailBar->selectedIndices()) {
        if (idx >= 0 && idx < m_files.size()) {
            paths.append(m_files.at(idx));
        }
    }
    m_imageView->setWorkspacePaths(paths);
    updateStatus();
}

void MainWindow::syncThumbnailWorkspaceSelection()
{
    // Selection is no longer forced to match canvas membership (double-click
    // and drag-and-drop own membership; strip selection is independent).
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
    // Always drive from the real window state so leave-fullscreen cannot stick
    // when a checkable action's checked flag desyncs from the WM.
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
    if (isWorkspaceMode()) {
        return;
    }
    m_workspaceModeAct->setChecked(true);
    m_imageView->setViewMode(ImageView::ViewMode::Workspace);
    m_thumbnailBar->setMultiSelectEnabled(true);
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

void MainWindow::resetItemScale()
{
    m_imageView->resetItemScale();
}

void MainWindow::resetItemRotation()
{
    m_imageView->resetItemRotation();
}

void MainWindow::duplicateSelected()
{
    m_imageView->duplicateSelected();
}

void MainWindow::clearWorkspaceExtras()
{
    m_imageView->clearExtras();
}

void MainWindow::toggleWorkspaceMode()
{
    const bool on = m_workspaceModeAct->isChecked();
    if (on) {
        stopSlideshow();
        m_galleryReturnActive = false;
        if (m_backToGalleryAct) {
            m_backToGalleryAct->setEnabled(false);
        }
        m_thumbnailBar->setMultiSelectEnabled(true);
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
                         }) {
            if (act) {
                act->setChecked(false);
            }
        }
    } else {
        m_thumbnailBar->setMultiSelectEnabled(false);
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
    // New dwell begins after advance — reset the pinned-HUD progress line.
    if (m_imageView && m_slideshowTimer && m_slideshowTimer->isActive()) {
        m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
    }
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

void MainWindow::showKeyboardShortcuts()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("Keyboard Shortcuts"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>Keyboard shortcuts</h3>"
        "<p><b>Navigation</b><br/>"
        "←/→ or edge click — previous / next<br/>"
        "Home / End — first / last<br/>"
        "Space — start/stop slideshow<br/>"
        "[ / ] — slower / faster slideshow (interval)<br/>"
        "Esc — leave fullscreen (or return to Gallery)</p>"
        "<p><b>View</b><br/>"
        "F / F11 — fullscreen<br/>"
        "H — toggle HUD<br/>"
        "Ctrl+0 — zoom 1:1 · Ctrl++ / Ctrl+- — zoom<br/>"
        "Ctrl+F — fill · Fit action — fit to window<br/>"
        "Ctrl+T — toolbar · Ctrl+E — metadata</p>"
        "<p><b>Image</b><br/>"
        "R / Ctrl+R — rotate right · Ctrl+L — rotate left<br/>"
        "Ctrl+H / Ctrl+Shift+H — flip horizontal / vertical</p>"
        "<p><b>Workspace</b><br/>"
        "Ctrl+D — duplicate · Delete — remove from canvas<br/>"
        "Ctrl+Shift+↑/↓ — raise / lower<br/>"
        "Ctrl+Shift+=/− — opacity up / down</p>"
        "<p><b>Gallery</b><br/>"
        "Click — select · Ctrl/Shift — multi-select · Double-click / Enter — open<br/>"
        "Arrow keys — spatial focus among tiles</p>"));
    box.setStandardButtons(QMessageBox::Close);
    box.button(QMessageBox::Close)->setText(tr("&Close"));
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
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
    dlg.setSortModeIndex(static_cast<int>(m_sortMode));
    dlg.setStartInWorkspaceMode(m_startInWorkspaceMode);
    dlg.setImageModeLeftDragPan(m_imageView->imageModeLeftDragPan());
    dlg.setBackgroundColor(m_imageView->backgroundColor());
    dlg.setBackgroundColorAlt(m_imageView->backgroundColorAlt());
    dlg.setBackgroundPatternIndex(
        m_imageView->backgroundPattern() == ImageView::BackgroundPattern::Checkerboard ? 1 : 0);
    dlg.setCheckerboardWorkspaceOnly(m_imageView->checkerboardWorkspaceOnly());
    dlg.setHudFontPointSize(m_imageView->hudFontPointSize());
    dlg.setHudTextColor(m_imageView->hudTextColor());
    dlg.setHudPanelColor(m_imageView->hudPanelColor());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    setSlideshowIntervalMs(dlg.slideshowIntervalMs());
    m_slideshowFullscreen = dlg.slideshowFullscreen();
    {
        const int si = dlg.sortModeIndex();
        SortMode mode = SortMode::Name;
        if (si >= 0 && si <= 5) {
            mode = static_cast<SortMode>(si);
        }
        setSortMode(mode);
    }
    m_startInWorkspaceMode = dlg.startInWorkspaceMode();
    m_imageView->setImageModeLeftDragPan(dlg.imageModeLeftDragPan());
    m_imageView->setBackgroundColor(dlg.backgroundColor());
    m_imageView->setBackgroundColorAlt(dlg.backgroundColorAlt());
    m_imageView->setBackgroundPattern(
        dlg.backgroundPatternIndex() == 1 ? ImageView::BackgroundPattern::Checkerboard
                                          : ImageView::BackgroundPattern::Solid);
    m_imageView->setCheckerboardWorkspaceOnly(dlg.checkerboardWorkspaceOnly());
    m_imageView->setHudFontPointSize(dlg.hudFontPointSize());
    m_imageView->setHudTextColor(dlg.hudTextColor());
    m_imageView->setHudPanelColor(dlg.hudPanelColor());
    writeSettings();
}

void MainWindow::selectAllThumbnails()
{
    if (!m_thumbnailBar || m_files.isEmpty()) {
        return;
    }
    // Session multi-select only — do not enter Workspace mode.
    m_thumbnailBar->selectAllThumbs();
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
    // Session index on ImageView so status bar and on-image HUD share n/N.
    if (m_imageView) {
        // Silent while the slideshow timer advances; user Next/Prev still pulse.
        m_imageView->setSessionPosition(m_currentIndex, m_files.size(),
                                        !m_slideshowAdvancing);
        const QString err = m_imageView->lastLoadError();
        if (!err.isEmpty() && statusBar()) {
            statusBar()->showMessage(
                tr("Could not load “%1”").arg(QFileInfo(err).fileName()), 5000);
        }
    }
    m_statusLabel->setText(m_imageView->statusText());
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
    if (c.alpha() < 255) {
        m_mouseLabel->setText(
            tr("(%1, %2)  RGBA %3 %4 %5 %6")
                .arg(info.imagePos.x())
                .arg(info.imagePos.y())
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue())
                .arg(c.alpha()));
    } else {
        m_mouseLabel->setText(
            tr("(%1, %2)  RGB %3 %4 %5")
                .arg(info.imagePos.x())
                .arg(info.imagePos.y())
                .arg(c.red())
                .arg(c.green())
                .arg(c.blue()));
    }
    if (m_colorSwatch) {
        m_colorSwatch->setStyleSheet(
            QStringLiteral("QLabel { border: 1px solid #888; background-color: %1; }")
                .arg(c.name(QColor::HexArgb)));
        m_colorSwatch->setToolTip(
            c.alpha() < 255
                ? tr("RGBA %1 %2 %3 %4 (%5)")
                      .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha())
                      .arg(c.name(QColor::HexArgb))
                : tr("RGB %1 %2 %3 (%4)")
                      .arg(c.red()).arg(c.green()).arg(c.blue())
                      .arg(c.name()));
    }
}

void MainWindow::showContextMenu(const QPoint &pos)
{
    // Mode-filtered context menu (AUDIT L29 polish).
    QMenu menu(this);
    menu.addAction(m_openAct);
    menu.addAction(m_addAct);
    menu.addSeparator();
    if (isImageMode()) {
        menu.addAction(m_previousAct);
        menu.addAction(m_nextAct);
        menu.addSeparator();
    }
    menu.addAction(m_zoomFitAct);
    menu.addAction(m_zoom1to1Act);
    if (!isGalleryMode()) {
        menu.addSeparator();
        menu.addAction(m_rotateLeftAct);
        menu.addAction(m_rotateRightAct);
        menu.addAction(m_flipHAct);
        menu.addAction(m_flipVAct);
        if (isWorkspaceMode()) {
            menu.addAction(m_resetScaleAct);
            menu.addAction(m_resetRotationAct);
        }
    }
    menu.addSeparator();
    if (m_backToGalleryAct && m_backToGalleryAct->isEnabled()) {
        menu.addAction(m_backToGalleryAct);
    }
    menu.addAction(m_workspaceModeAct);
    if (isWorkspaceMode()) {
        menu.addAction(m_raiseAct);
        menu.addAction(m_lowerAct);
    }
    menu.addSeparator();
    menu.addAction(m_fullscreenAct);
    menu.addAction(m_preferencesAct);
    menu.exec(m_imageView->mapToGlobal(pos));
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
        // Ensure arrow keys reach ImageView (navigation), not a hidden chrome widget.
        if (m_imageView) {
            m_imageView->setFocus(Qt::OtherFocusReason);
        }
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
        if (isWorkspaceMode() && m_workspaceToolBar) {
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
    } else if (sort == QLatin1String("filesize")) {
        m_sortMode = SortMode::FileSize;
    } else if (sort == QLatin1String("width")) {
        m_sortMode = SortMode::Width;
    } else if (sort == QLatin1String("height")) {
        m_sortMode = SortMode::Height;
    } else if (sort == QLatin1String("pixels")) {
        m_sortMode = SortMode::PixelCount;
    } else {
        m_sortMode = SortMode::Name;
    }
    setSortMode(m_sortMode); // checks the matching action (may re-sort empty list)

    setSlideshowIntervalMs(
        settings.value(QStringLiteral("slideshowIntervalMs"), 3000).toInt());
    const int masonryCols = settings.value(QStringLiteral("masonryColumns"), 3).toInt();
    const int gridCols = settings.value(QStringLiteral("gridColumns"), 0).toInt();
    const int masonryRows = settings.value(QStringLiteral("masonryRows"), 3).toInt();
    if (m_imageView) {
        m_imageView->setMasonryColumns(masonryCols);
        m_imageView->setGridColumns(gridCols);
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
    {
        const int layoutInt = settings.value(QStringLiteral("lastGalleryLayout"), -1).toInt();
        if (layoutInt >= int(ImageView::LayoutMode::SideBySide)
            && layoutInt <= int(ImageView::LayoutMode::MasonryRows)) {
            m_galleryReturnLayout = static_cast<ImageView::LayoutMode>(layoutInt);
        }
    }
    // Migrate legacy key if present and new key never set
    if (!settings.contains(QStringLiteral("startInWorkspaceMode"))
        && settings.contains(QStringLiteral("workspaceMode"))) {
        // Do not migrate "true" from a previous session toggle — always prefer off
        settings.remove(QStringLiteral("workspaceMode"));
        m_startInWorkspaceMode = false;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(m_startInWorkspaceMode);
    }
    if (m_imageView) {
        m_imageView->setViewMode(m_startInWorkspaceMode
                                     ? ImageView::ViewMode::Workspace
                                     : ImageView::ViewMode::Image);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(m_startInWorkspaceMode);
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
        m_imageView->setHudFontPointSize(
            settings.value(QStringLiteral("hudFontPointSize"), 11).toInt());
        {
            const QColor tc(settings.value(QStringLiteral("hudTextColor"),
                                           QStringLiteral("#ffffff")).toString());
            if (tc.isValid()) {
                m_imageView->setHudTextColor(tc);
            }
            // HexArgb is #AARRGGBB (Qt). Legacy #RRGGBBAA with alpha trailing is
            // rejected by the length-8 parser as fully transparent — migrate.
            const QString pcStr = settings.value(QStringLiteral("hudPanelColor"),
                                                 QStringLiteral("#a0000000")).toString();
            QColor pc(pcStr);
            if (!pc.isValid() || pc.alpha() == 0) {
                // Recover common mis-saved form #000000XX (RRGGBB + alpha byte)
                if (pcStr.size() == 9 && pcStr.startsWith(QLatin1Char('#'))) {
                    const QString rgb = pcStr.mid(1, 6);
                    const QString aa = pcStr.mid(7, 2);
                    pc = QColor(QStringLiteral("#") + aa + rgb);
                }
            }
            if (pc.isValid() && pc.alpha() > 0) {
                m_imageView->setHudPanelColor(pc);
            } else if (pc.isValid() && pc.alpha() == 0) {
                // Never leave a fully transparent panel as the loaded preference.
                m_imageView->setHudPanelColor(QColor(0, 0, 0, 160));
            }
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
    QString sortKey = QStringLiteral("name");
    switch (m_sortMode) {
    case SortMode::MTime: sortKey = QStringLiteral("mtime"); break;
    case SortMode::FileSize: sortKey = QStringLiteral("filesize"); break;
    case SortMode::Width: sortKey = QStringLiteral("width"); break;
    case SortMode::Height: sortKey = QStringLiteral("height"); break;
    case SortMode::PixelCount: sortKey = QStringLiteral("pixels"); break;
    case SortMode::Name:
    default: sortKey = QStringLiteral("name"); break;
    }
    settings.setValue(QStringLiteral("sortMode"), sortKey);
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
    if (m_imageView) {
        settings.setValue(QStringLiteral("gridColumns"), m_imageView->gridColumns());
        settings.setValue(QStringLiteral("masonryColumns"), m_imageView->masonryColumns());
        settings.setValue(QStringLiteral("masonryRows"), m_imageView->masonryRows());
    }
    if (m_imageView && m_imageView->isGalleryMode()) {
        settings.setValue(QStringLiteral("lastGalleryLayout"),
                          static_cast<int>(m_imageView->layoutMode()));
    } else if (m_galleryReturnActive) {
        settings.setValue(QStringLiteral("lastGalleryLayout"),
                          static_cast<int>(m_galleryReturnLayout));
    }
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
        settings.setValue(QStringLiteral("hudFontPointSize"), m_imageView->hudFontPointSize());
        settings.setValue(QStringLiteral("hudTextColor"), m_imageView->hudTextColor().name(QColor::HexArgb));
        settings.setValue(QStringLiteral("hudPanelColor"), m_imageView->hudPanelColor().name(QColor::HexArgb));
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

    // Workspace mode: append novel paths to the session; place or move on canvas
    // at the drop point (drag from the thumbnail bar lands where expected).
    if (isWorkspaceMode()) {
        const QStringList expanded = expandPaths(paths);
        if (expanded.isEmpty()) {
            return;
        }
        QStringList novel;
        for (const QString &p : expanded) {
            if (!m_files.contains(p)) {
                novel.append(p);
            }
        }
        if (!novel.isEmpty()) {
            appendFiles(novel);
        }
        int i = 0;
        for (const QString &img : expanded) {
            if (!scenePos.isNull()) {
                const QPointF pos = scenePos + QPointF(28.0 * i, 22.0 * i);
                m_imageView->placeOrMoveImageAt(img, pos);
            } else {
                m_imageView->addImage(img);
            }
            ++i;
        }
        if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
        updateStatus();
        return;
    }

    // Gallery mode: always append and relayout. Ignore drops that only re-state
    // paths already in the session (e.g. drag from the thumbnail bar) so tiles
    // are not duplicated.
    if (isGalleryMode()) {
        const QStringList expanded = expandPaths(paths);
        if (expanded.isEmpty()) {
            return;
        }
        QStringList novel;
        for (const QString &p : expanded) {
            if (!m_files.contains(p)) {
                novel.append(p);
            }
        }
        if (novel.isEmpty()) {
            return;
        }
        appendFiles(novel);
        // appendFiles updates the session list; rebuild the packed canvas.
        const ImageView::LayoutMode layout = m_imageView
            ? m_imageView->layoutMode()
            : ImageView::LayoutMode::Masonry;
        populateGalleryCanvas();
        if (m_imageView) {
            m_imageView->enterGallery(layout);
        }
        return;
    }

    // Image mode: always append to the session (Open still replaces).
    // Drops from the thumbnail bar are already in the session — just navigate
    // to the first path instead of wiping the session down to one file.
    const QStringList expanded = expandPaths(paths);
    if (expanded.isEmpty()) {
        return;
    }
    QStringList novel;
    for (const QString &p : expanded) {
        if (!m_files.contains(p)) {
            novel.append(p);
        }
    }
    if (!novel.isEmpty()) {
        appendFiles(novel);
    }
    // Focus the first dropped path (existing or newly appended).
    const QString focus = expanded.first();
    const int idx = m_files.indexOf(focus);
    if (idx >= 0) {
        setCurrentIndex(idx);
    }
    Q_UNUSED(modifiers);
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
