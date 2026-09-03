#include "archivepath.h"
// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"
#include "imageitem.h"

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
            this, &MainWindow::returnFromImageMode);
    connect(m_imageView, &ImageView::cropModeChanged, this, [this](bool on) {
        if (m_cropAct) {
            m_cropAct->setChecked(on);
        }
    });
    connect(m_imageView,
            QOverload<SessionImageId, const QString &, const QImage &>::of(
                &ImageView::sessionAppearanceChanged),
            this,
            [this](SessionImageId id, const QString &path, const QImage &image) {
                if (m_thumbnailBar) {
                    m_thumbnailBar->setSessionImageOverride(id, path, image);
                }
            });
    connect(m_imageView,
            QOverload<SessionImageId, const QString &, const QImage &>::of(
                &ImageView::sessionCropApplied),
            this,
            [this](SessionImageId id, const QString &path, const QImage &image) {
                if (m_thumbnailBar) {
                    m_thumbnailBar->setSessionImageOverride(id, path, image);
                }
            });
    connect(m_imageView, &ImageView::fullscreenToggleRequested,
            this, &MainWindow::toggleFullscreen);
    connect(m_imageView, &ImageView::galleryItemOpenRequested,
            this, &MainWindow::openGalleryItemInImageMode);
    connect(m_imageView, &ImageView::sessionImageOpenRequested,
            this, &MainWindow::openSessionImageInImageMode);
    connect(m_imageView, &ImageView::sessionSlotOpenRequested,
            this, &MainWindow::openSessionIndexInImageMode);
    connect(m_imageView, &ImageView::sessionRemovePathsRequested,
            this, &MainWindow::removeSessionPaths);
    connect(m_imageView, &ImageView::sessionRemoveIdsRequested,
            this, &MainWindow::removeSessionIds);
    connect(m_imageView, &ImageView::sessionImageFocused,
            this, [this](SessionImageId id) {
                const int idx = indexOfSessionId(id);
                if (idx >= 0) {
                    setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                }
            });
    connect(m_imageView, &ImageView::galleryItemFocused,
            this, [this](const QString &path) {
                const int idx = m_session.paths().indexOf(path);
                if (idx >= 0) {
                    // Path fallback for unbound tiles.
                    setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
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
    connect(m_imageView, &ImageView::canvasSelectionChanged, this, [this]() {
        // Crop and other selection-sensitive actions depend on target count.
        updateNavigationActions();
        updateWorkspaceActionVisibility();
        updateLayoutPanel();
        if (m_syncingSelection || !isWorkspaceMode() || !m_thumbnailBar || !m_imageView) {
            return;
        }
        m_syncingSelection = true;
        m_thumbnailBar->setSelectedIndices(m_imageView->selectedSessionIndices());
        m_syncingSelection = false;
    });

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

    m_adjustmentsPanel = new AdjustmentsPanel(this);
    m_adjustmentsDock = new QDockWidget(tr("Adjustments"), this);
    m_adjustmentsDock->setObjectName(QStringLiteral("AdjustmentsDock"));
    m_adjustmentsDock->setWidget(m_adjustmentsPanel);
    m_adjustmentsDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_adjustmentsDock->setFeatures(QDockWidget::DockWidgetClosable
                                   | QDockWidget::DockWidgetMovable
                                   | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::RightDockWidgetArea, m_adjustmentsDock);
    m_adjustmentsDock->hide();
    connect(m_adjustmentsPanel, &AdjustmentsPanel::adjustmentsChanged,
            this, [this](const ColorAdjustments &adj) {
                if (m_imageView) {
                    m_imageView->setTargetColorAdjustments(adj);
                    if (m_adjustmentsPanel) {
                        ImageItem *item = m_imageView->targetItem();
                        if (!item && !m_imageView->liveItems().isEmpty() && m_imageView->isImageMode()) {
                            item = m_imageView->liveItems().first();
                        }
                        if (item) {
                            m_adjustmentsPanel->setPreviewImage(item->pixmap().toImage());
                        }
                    }
                }
            });

    m_layoutPanel = new LayoutPanel(this);
    m_layoutDock = new QDockWidget(tr("Layout"), this);
    m_layoutDock->setObjectName(QStringLiteral("LayoutDock"));
    m_layoutDock->setWidget(m_layoutPanel);
    m_layoutDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_layoutDock->setFeatures(QDockWidget::DockWidgetClosable
                              | QDockWidget::DockWidgetMovable
                              | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_layoutDock);
    m_layoutDock->hide();
    connect(m_layoutPanel, &LayoutPanel::applyRequested,
            this, &MainWindow::applyWorkspaceLayoutFromPanel);

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
        if (m_imageView && m_imageView->isCropMode()) {
            m_imageView->cancelCrop();
            return;
        }
        if (isFullScreen()) {
            showNormal();
            return;
        }
        if ((m_galleryReturnActive || m_workspaceReturnActive)
            && m_imageView && m_imageView->isImageMode()) {
            returnFromImageMode();
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
        if (index >= 0 && index < m_session.paths().size()) {
            m_thumbnailBar->setSelectedIndices({index});
        }
    }
    if (index < 0 || index >= m_session.paths().size()) {
        return;
    }
    m_imageView->addImageForSession(m_session.paths().at(index), sessionIdAt(index), index);
    markWorkspaceDirty();
    syncThumbnailCanvasMembership();
}

void MainWindow::onThumbnailWorkspaceSelectionChanged()
{
    if (!isWorkspaceMode()) {
        return;
    }
    const QList<int> sel = m_thumbnailBar->selectedIndices();
    if (!sel.isEmpty()) {
        const int idx = sel.last();
        if (idx != m_currentIndex && idx >= 0 && idx < m_session.paths().size()) {
            m_currentIndex = idx;
            if (m_metadataPanel) {
                m_metadataPath = m_session.paths().at(m_currentIndex);
                m_metadataPanel->setImagePath(m_metadataPath);
            }
        }
    }
    // Shared selection: filmstrip multi-select drives canvas selection by session slot.
    if (!m_syncingSelection && m_imageView) {
        m_syncingSelection = true;
        m_imageView->selectBySessionIndices(sel);
        m_syncingSelection = false;
    }
    updateStatus();
}

void MainWindow::onThumbnailCanvasMembershipToggled(int index)
{
    if (index < 0 || index >= m_session.paths().size() || !m_imageView) {
        return;
    }
    if (!isWorkspaceMode()) {
        m_workspaceModeAct->setChecked(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        m_thumbnailBar->setMultiSelectEnabled(true);
        updateWorkspaceActionVisibility();
    }
    const QString path = m_session.paths().at(index);
    const SessionImageId sid = sessionIdAt(index);
    // Identity is SessionImageId only — never path / occurrence (duplicates).
    if (sid != kInvalidSessionImageId && m_imageView->findItemBySessionId(sid)) {
        m_imageView->detachCanvasSessionId(sid);
    } else if (sid == kInvalidSessionImageId
               && m_imageView->hasWorkspaceSessionIndex(index)) {
        m_imageView->removeWorkspaceSessionIndex(index);
    } else {
        m_imageView->addImageForSession(path, sid, index);
    }
    markWorkspaceDirty();
    syncThumbnailCanvasMembership();
    updateStatus();
}

void MainWindow::onWorkspacePathsChanged()
{
    if (!isWorkspaceMode()) {
        return;
    }
    markWorkspaceDirty();
    syncThumbnailCanvasMembership();
    updateStatus();
}

void MainWindow::syncCanvasFromThumbnailSelection()
{
    // Kept for callers that still expect a bulk "selection → canvas" path
    // (e.g. future context-menu actions). Not used for ordinary clicks.
    QStringList paths;
    for (int idx : m_thumbnailBar->selectedIndices()) {
        if (idx >= 0 && idx < m_session.paths().size()) {
            paths.append(m_session.paths().at(idx));
        }
    }
    m_imageView->setWorkspacePaths(paths);
    updateStatus();
}

void MainWindow::syncThumbnailWorkspaceSelection()
{
    // Kept for callers; membership badge is syncThumbnailCanvasMembership().
}

void MainWindow::syncThumbnailCanvasMembership()
{
    if (!m_thumbnailBar || !m_imageView) {
        return;
    }
    // Ensure every on-canvas item is tied to a session row (badges + shared selection).
    m_imageView->rebindWorkspaceSession(m_session.paths(), m_session.ids());

    // After rebind demotes duplicate SessionImageIds, allocate a fresh session
    // image for each still-unbound live tile so two Workspace tiles never share
    // one filmstrip row / one appearance slot.
    bool grew = false;
    for (ImageItem *item : m_imageView->liveItems()) {
        if (!item) {
            continue;
        }
        if (item->sessionId() != kInvalidSessionImageId
            && m_session.indexOfId(item->sessionId()) >= 0) {
            continue;
        }
        const QString path = item->path();
        if (path.isEmpty()) {
            continue;
        }
        const SessionImageId id = allocSessionId();
        m_session.append(path, id);
        item->setSessionId(id);
        item->setSessionIndex(m_session.size() - 1);
        // Preserve current pixels as the new session image's appearance.
        m_imageView->commitItemSessionEdit(item);
        grew = true;
    }
    if (grew) {
        m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        m_imageView->rebindWorkspaceSession(m_session.paths(), m_session.ids());
    }

    // Badge by stable id → session row, not path (duplicate-safe).
    QSet<int> onCanvas;
    for (ImageItem *item : m_imageView->liveItems()) {
        if (!item) {
            continue;
        }
        int idx = -1;
        if (item->sessionId() != kInvalidSessionImageId) {
            idx = m_session.indexOfId(item->sessionId());
        }
        if (idx < 0) {
            idx = item->sessionIndex();
        }
        if (idx >= 0) {
            onCanvas.insert(idx);
        }
    }
    m_thumbnailBar->setOnCanvasIndices(onCanvas);
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

void MainWindow::toggleCropMode()
{
    if (!m_imageView) {
        return;
    }
    const bool want = m_cropAct && m_cropAct->isChecked();
    // Gallery: crop on the packed grid is unusable — open the subject in Image
    // mode, then enter crop once pixels are ready.
    if (want && m_imageView->isGalleryMode()) {
        ImageItem *item = m_imageView->targetItem();
        if (!item || !m_imageView->hasSingleCropTarget()) {
            if (m_cropAct) {
                m_cropAct->setChecked(false);
            }
            return;
        }
        int idx = -1;
        if (item->sessionId() != kInvalidSessionImageId) {
            idx = indexOfSessionId(item->sessionId());
        }
        if (idx < 0) {
            idx = item->sessionIndex();
        }
        if (idx < 0) {
            if (m_cropAct) {
                m_cropAct->setChecked(false);
            }
            return;
        }
        m_pendingGalleryCrop = true;
        openSessionIndexInImageMode(idx);
        // LoadReplace is async; one-shot when Image mode has decoded pixels.
        auto *conn = new QMetaObject::Connection;
        *conn = QObject::connect(
            m_imageView, &ImageView::statusChanged, this,
            [this, conn]() {
                if (!m_pendingGalleryCrop || !m_imageView
                    || !m_imageView->isImageMode()) {
                    return;
                }
                ImageItem *primary = m_imageView->targetItem();
                if (!primary && !m_imageView->liveItems().isEmpty()) {
                    primary = m_imageView->liveItems().first();
                }
                if (!primary || !primary->hasDecodedPixels()) {
                    return;
                }
                m_pendingGalleryCrop = false;
                QObject::disconnect(*conn);
                delete conn;
                m_imageView->setCropMode(true);
                if (m_cropAct) {
                    m_cropAct->setChecked(m_imageView->isCropMode());
                }
            });
        if (m_cropAct) {
            // Stay unchecked until crop actually opens.
            m_cropAct->setChecked(false);
        }
        return;
    }
    m_pendingGalleryCrop = false;
    m_imageView->setCropMode(want);
    if (m_cropAct) {
        m_cropAct->setChecked(m_imageView->isCropMode());
    }
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

void MainWindow::toggleThumbnailCrop()
{
    if (!m_thumbnailBar || !m_cropThumbnailsAct) {
        return;
    }
    m_thumbnailBar->setCropToSquare(m_cropThumbnailsAct->isChecked());
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
        && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        m_imageView->addImage(m_session.paths().at(m_currentIndex));
    }
    syncThumbnailWorkspaceSelection();
    if (m_session.paths().size() > 1 && !m_thumbnailBar->isVisible()) {
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

void MainWindow::resetItemShear()
{
    if (m_imageView) {
        m_imageView->resetItemShear();
    }
}

void MainWindow::openSelectionInNewWindow()
{
    if (!m_imageView) {
        return;
    }
    const QStringList paths = m_imageView->selectedPaths();
    if (paths.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("Nothing selected to open in a new window."), 3000);
        }
        return;
    }
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->show();
    window->loadFiles(paths);
}

void MainWindow::toggleWorkspaceMode()
{
    const bool on = m_workspaceModeAct->isChecked();
    if (on) {
        stopSlideshow();
        m_galleryReturnActive = false;
        m_workspaceReturnActive = false;
        if (m_backToGalleryAct) {
            m_backToGalleryAct->setEnabled(false);
        }
        m_thumbnailBar->setMultiSelectEnabled(true);
        m_imageView->setViewMode(ImageView::ViewMode::Workspace);
        // Workspace starts/stays empty unless the user (or a project) places tiles.
        // Do not seed from the session list — arrangement is permanent across modes.
        syncThumbnailCanvasMembership();
        // Thumbnail visibility for Workspace is applied in updateWorkspaceActionVisibility
        // via updateThumbnailBarForMode (default preferred on).
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
        if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
            m_imageView->loadImage(m_session.paths().at(m_currentIndex));
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
    // Remember preference for the mode the user is currently in so Gallery and
    // Workspace stay independent. Image mode still uses the force flags so
    // applyThumbnailVisibility keeps working after load/sort.
    if (m_imageView && m_imageView->isGalleryMode()) {
        m_thumbnailsPreferredGallery = visible;
        m_forceThumbnails = false;
        m_forceNoThumbnails = false;
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        m_thumbnailsPreferredWorkspace = visible;
        m_forceThumbnails = false;
        m_forceNoThumbnails = false;
    } else {
        m_forceNoThumbnails = !visible;
        m_forceThumbnails = visible;
    }
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
        "F5 — reload from disk (current image / all gallery tiles)<br/>"
        "Ctrl+0 — zoom 1:1 · Ctrl++ / Ctrl+- — zoom<br/>"
        "Ctrl+F — fill · Fit — fit to window · Z — zoom to region<br/>"
        "Ctrl+T — toolbar · Ctrl+M — thumbnails · Ctrl+E — metadata<br/>"
        "Ctrl+U — colour adjustments · F1 — this list</p>"
        "<p><b>Image</b><br/>"
        "R / Ctrl+R — rotate right · Ctrl+L — rotate left<br/>"
        "Ctrl+H / Ctrl+Shift+H — flip horizontal / vertical<br/>"
        "C — crop mode</p>"
        "<p><b>Workspace</b><br/>"
        "Ctrl+C / Ctrl+X / Ctrl+V — copy / cut / paste tiles<br/>"
        "Ctrl+D — duplicate · Delete — remove from canvas<br/>"
        "Ctrl+Shift+↑/↓ — raise / lower<br/>"
        "Ctrl+Shift+=/− — opacity up / down<br/>"
        "Alt+[ / Alt+] — nudge shear · Alt+0 — reset shear<br/>"
        "Group scale: edges H/V · corners uniform · Shift frees axes</p>"
        "<p><b>Gallery</b><br/>"
        "Click — select · Ctrl/Shift — multi-select · Double-click / Enter — open<br/>"
        "Arrow keys — spatial focus among tiles</p>"
        "<p><b>Files</b><br/>"
        "Ctrl+O — open · Ctrl+Shift+A — add · Ctrl+Shift+O — open project<br/>"
        "Ctrl+S / Ctrl+Shift+S — save / save project as<br/>"
        "Ctrl+Q — quit</p>"));
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
        tr("<p>A classic image viewer with Gallery overview and a free-form "
           "Workspace for comparing images.</p>"
           "<p>Copyright © 2026 Ingo Ruhnke &lt;grumbel@gmail.com&gt;<br/>"
           "License: GPL-3.0-or-later</p>"));
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
    dlg.setScrollBarsVisible(m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked());
    dlg.setThumbnailLabelsVisible(m_hideThumbLabelsAct && !m_hideThumbLabelsAct->isChecked());
    dlg.setAdjustmentsPanelVisible(m_adjustmentsDock && m_adjustmentsDock->isVisible());
    dlg.setLayoutPanelPreferredInWorkspace(m_layoutPreferredInWorkspace);
    dlg.setThumbnailsPreferredWorkspace(m_thumbnailsPreferredWorkspace);
    dlg.setThumbnailsPreferredGallery(m_thumbnailsPreferredGallery);
    {
        int pos = 0;
        if (m_thumbnailEdge == ThumbnailEdge::Top) pos = 1;
        else if (m_thumbnailEdge == ThumbnailEdge::Left) pos = 2;
        else if (m_thumbnailEdge == ThumbnailEdge::Right) pos = 3;
        dlg.setThumbnailPositionIndex(pos);
    }
    {
        int layoutMode = static_cast<int>(ImageView::LayoutMode::Masonry);
        if (m_imageView && m_imageView->isGalleryMode()) {
            layoutMode = static_cast<int>(m_imageView->layoutMode());
        } else if (m_galleryReturnActive) {
            layoutMode = static_cast<int>(m_galleryReturnLayout);
        } else {
            QSettings settings;
            layoutMode = settings.value(QStringLiteral("lastGalleryLayout"), layoutMode).toInt();
        }
        dlg.setDefaultGalleryLayoutMode(layoutMode);
    }
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

    if (m_toggleScrollBarsAct) {
        m_toggleScrollBarsAct->setChecked(dlg.scrollBarsVisible());
        toggleScrollBars();
    }
    if (m_hideThumbLabelsAct) {
        m_hideThumbLabelsAct->setChecked(!dlg.thumbnailLabelsVisible());
        if (m_thumbnailBar) {
            m_thumbnailBar->setLabelsVisible(dlg.thumbnailLabelsVisible());
        }
    }
    if (m_adjustmentsDock) {
        const bool showAdj = dlg.adjustmentsPanelVisible();
        m_adjustmentsDock->setVisible(showAdj);
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(showAdj);
        }
    }
    m_layoutPreferredInWorkspace = dlg.layoutPanelPreferredInWorkspace();
    m_thumbnailsPreferredWorkspace = dlg.thumbnailsPreferredWorkspace();
    m_thumbnailsPreferredGallery = dlg.thumbnailsPreferredGallery();
    updateLayoutPanelForMode();
    updateThumbnailBarForMode();
    {
        const int pos = dlg.thumbnailPositionIndex();
        ThumbnailEdge edge = ThumbnailEdge::Bottom;
        if (pos == 1) edge = ThumbnailEdge::Top;
        else if (pos == 2) edge = ThumbnailEdge::Left;
        else if (pos == 3) edge = ThumbnailEdge::Right;
        setThumbnailBarPosition(edge);
    }
    {
        const int layoutMode = dlg.defaultGalleryLayoutMode();
        m_galleryReturnLayout = static_cast<ImageView::LayoutMode>(layoutMode);
        QSettings settings;
        settings.setValue(QStringLiteral("lastGalleryLayout"), layoutMode);
        if (m_imageView && m_imageView->isGalleryMode()) {
            m_imageView->setLayoutMode(static_cast<ImageView::LayoutMode>(layoutMode));
        }
    }
    writeSettings();
}

void MainWindow::selectAllThumbnails()
{
    if (!m_thumbnailBar || m_session.paths().isEmpty()) {
        return;
    }
    // Session multi-select only — do not enter Workspace mode.
    m_thumbnailBar->selectAllThumbs();
}

void MainWindow::updateWindowTitle()
{
    if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        const QString name = ArchivePath::displayName(m_session.paths().at(m_currentIndex));
        // GNOME-style: document name first, then app
        setWindowTitle(tr("%1 — QImgView").arg(name));
    } else {
        setWindowTitle(tr("QImgView"));
    }
}

void MainWindow::updateMetadataPanel()
{
    if (!m_metadataPanel || !m_imageView) {
        return;
    }
    QString path;
    // Prefer canvas selection (Workspace / Gallery); else session index.
    const QStringList selected = m_imageView->selectedPaths();
    if (!selected.isEmpty()) {
        path = selected.first();
    } else if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path == m_metadataPath) {
        return;
    }
    m_metadataPath = path;
    if (path.isEmpty()) {
        m_metadataPanel->clear();
    } else {
        m_metadataPanel->setImagePath(path);
    }
}

void MainWindow::updateAdjustmentsPanel()
{
    if (!m_adjustmentsPanel || !m_imageView) {
        return;
    }
    ImageItem *item = m_imageView->targetItem();
    if (!item && !m_imageView->liveItems().isEmpty() && m_imageView->isImageMode()) {
        item = m_imageView->liveItems().first();
    }
    if (!item || !item->hasDecodedPixels()) {
        m_adjustmentsPanel->clearPreview();
        m_adjustmentsPanel->setEnabledControls(false);
        return;
    }
    m_adjustmentsPanel->setEnabledControls(true);
    {
        QSignalBlocker b(m_adjustmentsPanel);
        m_adjustmentsPanel->setAdjustments(item->colorAdjustments());
    }
    m_adjustmentsPanel->setPreviewImage(item->pixmap().toImage());
}


void MainWindow::updateLayoutPanel()
{
    if (!m_layoutPanel || !m_imageView) {
        return;
    }
    const bool workspace = m_imageView->isWorkspaceMode();
    m_layoutPanel->setWorkspaceActive(workspace);
    int count = 0;
    if (workspace && m_imageView->canvasScene()) {
        for (QGraphicsItem *gi : m_imageView->canvasScene()->selectedItems()) {
            if (qgraphicsitem_cast<ImageItem *>(gi)) {
                ++count;
            }
        }
    }
    m_layoutPanel->setSelectionCount(count);
}

void MainWindow::applyWorkspaceLayoutFromPanel()
{
    if (!m_layoutPanel || !m_imageView || !m_imageView->isWorkspaceMode()) {
        return;
    }
    if (m_imageView->layoutWorkspaceItems(m_layoutPanel->params())) {
        markWorkspaceDirty();
        if (statusBar()) {
            statusBar()->showMessage(tr("Layout applied to selection."), 2500);
        }
    }
}

void MainWindow::updateStatus()
{
    updateNavigationActions();
    updateMetadataPanel();
    updateAdjustmentsPanel();
    // Session index on ImageView so status bar and on-image HUD share n/N.
    if (m_imageView) {
        // Silent while the slideshow timer advances; user Next/Prev still pulse.
        m_imageView->setSessionPosition(m_currentIndex, m_session.paths().size(),
                                        !m_slideshowAdvancing);
        m_imageView->setCurrentSessionId(currentSessionId());
        const QString err = m_imageView->lastLoadError();
        if (!err.isEmpty() && statusBar()) {
            statusBar()->showMessage(
                tr("Could not load “%1”").arg(ArchivePath::displayName(err)), 5000);
        }
        // Drag/open decode progress (also covers Gallery virtualization window).
        const int pending = m_imageView->pendingDecodeCount();
        if (pending > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("Decoding %n image…", "decode progress", pending), 0);
        } else if (statusBar() && statusBar()->currentMessage().startsWith(tr("Decoding "))) {
            statusBar()->clearMessage();
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
    if (m_openSelectionNewWindowAct) {
        menu.addAction(m_openSelectionNewWindowAct);
    }
    menu.addSeparator();
    if (isImageMode()) {
        menu.addAction(m_previousAct);
        menu.addAction(m_nextAct);
        menu.addSeparator();
    }
    menu.addAction(m_zoomFitAct);
    menu.addAction(m_zoom1to1Act);
    menu.addSeparator();
    menu.addAction(m_rotateLeftAct);
    menu.addAction(m_rotateRightAct);
    menu.addAction(m_flipHAct);
    menu.addAction(m_flipVAct);
    if (isWorkspaceMode()) {
        menu.addAction(m_resetScaleAct);
        if (m_resetShearAct) {
            menu.addAction(m_resetShearAct);
        }
        menu.addAction(m_resetRotationAct);
        menu.addAction(m_copyWorkspaceAct);
        menu.addAction(m_cutWorkspaceAct);
        menu.addAction(m_pasteWorkspaceAct);
        menu.addAction(m_duplicateAct);
        menu.addAction(m_raiseAct);
        menu.addAction(m_lowerAct);
    }
    menu.addSeparator();
    if (m_backToGalleryAct && m_backToGalleryAct->isEnabled()) {
        menu.addAction(m_backToGalleryAct);
    }
    // Workspace mode toggle stays on the main toolbar / Workspace menu only.
    menu.addSeparator();
    // Menu bar is hidden in fullscreen; surface slideshow control here.
    if (isFullScreen()) {
        menu.addAction(m_slideshowAct);
    }
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
    m_fullscreenAct->setText(fs ? tr("Exit &Fullscreen") : tr("F&ullscreen"));

    // Leaving fullscreen stops an active slideshow (Space starts; Esc stops)
    if (!fs && m_slideshowTimer && m_slideshowTimer->isActive()) {
        stopSlideshow();
    }

    if (fs) {
        m_toolBarVisibleBeforeFullscreen = m_toolBar->isVisible();
        m_thumbnailBarVisibleBeforeFullscreen = m_thumbnailBar->isVisible();
        m_metadataVisibleBeforeFullscreen =
            m_metadataDock && m_metadataDock->isVisible();
        m_layoutVisibleBeforeFullscreen =
            m_layoutDock && m_layoutDock->isVisible();
        m_toolBar->setVisible(false);
        if (m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(false);
        }
        m_thumbnailBar->setVisible(false);
        m_metadataDock->setVisible(false);
        if (m_layoutDock) {
            m_layoutDock->setVisible(false);
        }
        m_toggleToolBarAct->setChecked(false);
        m_toggleThumbnailBarAct->setChecked(false);
        m_toggleMetadataAct->setChecked(false);
        if (m_toggleLayoutPanelAct) {
            m_toggleLayoutPanelAct->setChecked(false);
        }
        menuBar()->setVisible(false);
        statusBar()->setVisible(false);
        // Ensure arrow keys reach ImageView (navigation), not a hidden chrome widget.
        if (m_imageView) {
            m_imageView->setFocus(Qt::OtherFocusReason);
        }
    } else {
        m_toolBar->setVisible(m_toolBarVisibleBeforeFullscreen);
        m_toggleToolBarAct->setChecked(m_toolBarVisibleBeforeFullscreen);
        if (m_metadataDock) {
            m_metadataDock->setVisible(m_metadataVisibleBeforeFullscreen);
        }
        if (m_toggleMetadataAct) {
            m_toggleMetadataAct->setChecked(m_metadataVisibleBeforeFullscreen);
        }
        // Thumbnails and Layout panel follow per-mode rules, not a single
        // pre-fullscreen snapshot (Gallery must not regain a Workspace layout dock).
        updateThumbnailBarForMode();
        updateLayoutPanelForMode();
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
    // Adjustments is opt-in: restoreState may re-show it from an old windowState.
    // Prefer an explicit setting (default: hidden).
    if (m_adjustmentsDock) {
        const bool showAdj =
            settings.value(QStringLiteral("adjustmentsPanelVisible"), false).toBool();
        m_adjustmentsDock->setVisible(showAdj);
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(showAdj);
        }
    }
    // Per-mode chrome preferences (defaults: Workspace thumbs on, Gallery off,
    // Layout panel off). restoreState may have re-shown docks — Layout is forced
    // through updateLayoutPanelForMode after mode is applied below.
    m_thumbnailsPreferredWorkspace =
        settings.value(QStringLiteral("thumbnailsPreferredWorkspace"), true).toBool();
    m_thumbnailsPreferredGallery =
        settings.value(QStringLiteral("thumbnailsPreferredGallery"), false).toBool();
    m_layoutPreferredInWorkspace =
        settings.value(QStringLiteral("layoutPreferredInWorkspace"), false).toBool();
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
            && layoutInt <= int(ImageView::LayoutMode::MasonryRowsFill)) {
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
        const bool cropThumbs = settings.value(QStringLiteral("thumbnailCropToSquare"), true).toBool();
        m_thumbnailBar->setCropToSquare(cropThumbs);
        if (m_cropThumbnailsAct) {
            m_cropThumbnailsAct->setChecked(cropThumbs);
        }
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

    // Session history (full path lists per open)
    m_sessionHistory.clear();
    const int histCount = settings.beginReadArray(QStringLiteral("sessionHistory"));
    for (int i = 0; i < histCount && i < kMaxSessionHistory; ++i) {
        settings.setArrayIndex(i);
        const QStringList paths = settings.value(QStringLiteral("paths")).toStringList();
        if (!paths.isEmpty()) {
            m_sessionHistory.append(paths);
        }
    }
    settings.endArray();
    rebuildHistoryMenu();

    m_recentProjects.clear();
    const QStringList recent = settings.value(QStringLiteral("recentProjects")).toStringList();
    for (const QString &p : recent) {
        if (!p.isEmpty() && m_recentProjects.size() < kMaxRecentProjects) {
            m_recentProjects.append(p);
        }
    }
    rebuildRecentProjectsMenu();
}

void MainWindow::writeSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.beginWriteArray(QStringLiteral("sessionHistory"), m_sessionHistory.size());
    for (int i = 0; i < m_sessionHistory.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("paths"), m_sessionHistory.at(i));
    }
    settings.endArray();
    settings.setValue(QStringLiteral("recentProjects"), m_recentProjects);
    settings.setValue(QStringLiteral("windowState"), saveState());
    if (m_adjustmentsDock) {
        settings.setValue(QStringLiteral("adjustmentsPanelVisible"),
                          m_adjustmentsDock->isVisible());
    }
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
    settings.setValue(QStringLiteral("thumbnailsPreferredWorkspace"),
                      m_thumbnailsPreferredWorkspace);
    settings.setValue(QStringLiteral("thumbnailsPreferredGallery"),
                      m_thumbnailsPreferredGallery);
    settings.setValue(QStringLiteral("layoutPreferredInWorkspace"),
                      m_layoutPreferredInWorkspace);
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
        settings.setValue(QStringLiteral("thumbnailCropToSquare"),
                          m_thumbnailBar->cropToSquare());
    }
}


void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_imageView && m_imageView->isCropMode()) {
            m_imageView->cancelCrop();
            event->accept();
            return;
        }
        if (isFullScreen()) {
            showNormal();
            event->accept();
            return;
        }
        if ((m_galleryReturnActive || m_workspaceReturnActive)
            && m_imageView && m_imageView->isImageMode()) {
            returnFromImageMode();
            event->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmQuitOrClose()) {
        event->ignore();
        return;
    }
    writeSettings();
    QMainWindow::closeEvent(event);
}

bool MainWindow::workspaceHasUnsavedWork() const
{
    if (!m_workspaceDirty) {
        return false;
    }
    return m_imageView && m_imageView->hasWorkspaceContent();
}

void MainWindow::markWorkspaceDirty()
{
    m_workspaceDirty = true;
}

bool MainWindow::confirmQuitOrClose()
{
    if (!workspaceHasUnsavedWork()) {
        return true;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Save Workspace before closing?"));
    box.setText(tr("The Workspace has images that have not been saved to a project."));
    box.setInformativeText(
        tr("If you close without saving, your arrangement will be lost."));
    QPushButton *discardBtn = box.addButton(tr("Close without Saving"),
                                            QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
    QPushButton *saveBtn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    box.setDefaultButton(saveBtn);
    box.exec();
    if (box.clickedButton() == cancelBtn) {
        return false;
    }
    if (box.clickedButton() == saveBtn) {
        saveProject();
        // User cancelled Save As or save failed — keep the window open.
        if (workspaceHasUnsavedWork()) {
            return false;
        }
        return true;
    }
    Q_UNUSED(discardBtn);
    return true;
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
                                   const QPointF &scenePos, const QList<qint64> &sessionIds)
{
    // Build a transient mime payload so extractLocalImagePaths stays the single filter
    QMimeData mime;
    mime.setUrls(urls);
    const QStringList paths = extractLocalImagePaths(&mime);
    if (paths.isEmpty()) {
        return;
    }

    // Workspace mode: place each drop as a canvas instance at the drop point.
    // Paths already on the canvas are *duplicated* (not moved); a new session
    // slot is appended so the filmstrip can address the copy independently.
    if (isWorkspaceMode()) {
        const QStringList expanded = expandPaths(paths);
        if (expanded.isEmpty()) {
            return;
        }
        QStringList novel;
        for (const QString &p : expanded) {
            if (!m_session.paths().contains(p)) {
                novel.append(p);
            }
        }
        if (!novel.isEmpty()) {
            appendFiles(novel);
        }
        int i = 0;
        for (const QString &img : expanded) {
            SessionImageId sid = kInvalidSessionImageId;
            int slot = -1;
            // Prefer identity from the filmstrip drag payload (duplicate-safe).
            if (i < sessionIds.size() && sessionIds.at(i) != 0
                && sessionIds.at(i) != static_cast<qint64>(kInvalidSessionImageId)) {
                sid = static_cast<SessionImageId>(sessionIds.at(i));
                slot = m_session.indexOfId(sid);
            }
            const SessionImageId sourceSid = sid;
            // Prefer session-id membership over path counts (duplicates share a path).
            const bool alreadyOnCanvas = (sourceSid != kInvalidSessionImageId)
                ? (m_imageView->findItemBySessionId(sourceSid) != nullptr)
                : (m_imageView->workspacePathOccurrenceCount(img) > 0);
            // Session image already on the canvas: allocate a new session image
            // (drop-duplicate) and copy content appearance so a cropped filmstrip
            // drag does not place a full-frame / wrong-looking twin.
            if (alreadyOnCanvas && (slot < 0 || m_imageView->findItemBySessionId(sid))) {
                m_session.append(img);
                const SessionImageId newSid = m_session.ids().isEmpty()
                    ? kInvalidSessionImageId
                    : m_session.ids().last();
                if (sourceSid != kInvalidSessionImageId && newSid != kInvalidSessionImageId) {
                    m_imageView->copySessionAppearance(sourceSid, newSid);
                }
                if (m_thumbnailBar) {
                    m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
                    m_thumbnailBar->setMultiSelectEnabled(true);
                }
                slot = m_session.size() - 1;
                sid = newSid;
            } else if (slot < 0) {
                slot = m_session.lastIndexOfPath(img);
                sid = sessionIdAt(slot);
            }
            if (!scenePos.isNull()) {
                const QPointF pos = scenePos + QPointF(28.0 * i, 22.0 * i);
                // placeOrMoveImageAt owns identity via PendingSessionBind / move-by-id.
                // Do NOT bindSelectedSessionIds here — that stamped sid onto every
                // currently selected tile and created duplicate SessionImageIds.
                m_imageView->placeOrMoveImageAt(img, pos, sid, slot);
            } else {
                m_imageView->addImageForSession(img, sid, slot);
            }
            ++i;
        }
        syncThumbnailCanvasMembership();
        if (m_session.paths().size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
        updateStatus();
        return;
    }

    // Gallery mode: each dropped path becomes a new session row (allows the
    // same file more than once). Thumbnail-bar drags therefore duplicate;
    // external files are appended as usual.
    if (isGalleryMode()) {
        const QStringList expanded = expandPaths(paths);
        if (expanded.isEmpty()) {
            return;
        }
        for (const QString &p : expanded) {
            if (p.isEmpty()) {
                continue;
            }
            m_session.append(p);
        }
        if (m_thumbnailBar) {
            m_thumbnailBar->setSession(m_session.paths(), m_session.ids());
        }
        applyThumbnailVisibility();
        const ImageView::LayoutMode layout = m_imageView
            ? m_imageView->layoutMode()
            : ImageView::LayoutMode::Masonry;
        populateGalleryCanvas();
        if (m_imageView) {
            m_imageView->enterGallery(layout);
        }
        updateStatus();
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
        if (!m_session.paths().contains(p)) {
            novel.append(p);
        }
    }
    if (!novel.isEmpty()) {
        appendFiles(novel);
    }
    // Focus the first dropped path (existing or newly appended).
    const QString focus = expanded.first();
    const int idx = m_session.paths().indexOf(focus);
    if (idx >= 0) {
        setCurrentIndex(idx);
    }
    Q_UNUSED(modifiers);
}

void MainWindow::onFilesDropped(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                const QPointF &scenePos, const QList<qint64> &sessionIds)
{
    handleDroppedUrls(urls, modifiers, scenePos, sessionIds);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData() || !event->mimeData()->hasUrls()) {
        return;
    }
    QList<qint64> sessionIds;
    const QByteArray idBytes =
        event->mimeData()->data(QStringLiteral("application/x-qimgview-session-ids"));
    if (!idBytes.isEmpty()) {
        for (const QByteArray &tok : idBytes.split(',')) {
            bool ok = false;
            const qint64 v = tok.trimmed().toLongLong(&ok);
            sessionIds.append(ok ? v : 0);
        }
    }
    // Window-level drop has no reliable scene position — empty-space placement
    handleDroppedUrls(event->mimeData()->urls(), event->modifiers(), QPointF(), sessionIds);
    event->acceptProposedAction();
}
