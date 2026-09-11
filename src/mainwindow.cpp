// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"
#include "version.h"
#include "imageitem.h"

#include <QDebug>
#include "biltoo_logging.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Biltoo"));
    setWindowIcon(QApplication::windowIcon());
    resize(1024, 768);
    setAcceptDrops(true);

    m_imageView = new ImageView(this);
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
    connect(m_imageView, &ImageView::slideshowTogglePauseRequested, this,
            [this]() {
                if (m_slideshowPaused) {
                    resumeSlideshow();
                } else if (m_slideshowClockRunning) {
                    pauseSlideshow();
                }
            });
    connect(m_imageView, &ImageView::navigatePreviousRequested,
            this, &MainWindow::goPrevious);
    connect(m_imageView, &ImageView::navigateNextRequested,
            this, &MainWindow::goNext);
    connect(m_imageView, &ImageView::linkActivated, this,
            [this](int page, const QString &uri) {
                if (page > 0) {
                    navigateDocumentPage(page);
                } else if (!uri.isEmpty()) {
                    openDocumentLinkUri(uri);
                }
            });

    connect(m_imageView, &ImageView::galleryReturnRequested,
            this, &MainWindow::returnFromImageMode);
    connect(m_imageView, &ImageView::cropModeChanged, this, [this](bool on) {
        if (m_cropAct) {
            m_cropAct->setChecked(on);
        }
    });
    connect(m_imageView, &ImageView::attentionModeChanged, this, [this](bool on) {
        if (m_attentionAct) {
            m_attentionAct->setChecked(on);
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
                if (idx < 0) {
                    return;
                }
                // Gallery: paint selection first; session cursor / filmstrip next tick.
                if (isGalleryMode()) {
                    QTimer::singleShot(0, this, [this, idx]() {
                        setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                    });
                } else {
                    setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                }
            });
    connect(m_imageView, &ImageView::galleryItemFocused,
            this, [this](const QString &path) {
                auto apply = [this](int idx) {
                    if (idx < 0) {
                        return;
                    }
                    if (isGalleryMode()) {
                        QTimer::singleShot(0, this, [this, idx]() {
                            setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                        });
                    } else {
                        setCurrentIndex(idx, /*ensureGalleryVisible=*/false);
                    }
                };
                // Prefer selected/sole live tile so duplicate paths focus the
                // correct session row (not paths().indexOf first match).
                if (ImageItem *pref = m_imageView->findPreferredItemForPath(path)) {
                    if (pref->sessionId() != kInvalidSessionImageId) {
                        const int idx = indexOfSessionId(pref->sessionId());
                        if (idx >= 0) {
                            apply(idx);
                            return;
                        }
                    }
                    if (pref->sessionIndex() >= 0) {
                        apply(pref->sessionIndex());
                        return;
                    }
                }
                apply(m_session.paths().indexOf(path));
            });
    connect(m_imageView, &ImageView::filesDropped,
            this, &MainWindow::onFilesDropped);

    m_thumbnailBar = new ThumbnailBar(this);
    m_thumbnailBar->setAccessibleName(tr("Thumbnails"));
    if (m_imageView) {
        m_thumbnailBar->setStripBackground(m_imageView->backgroundColor());
    }
    connect(m_thumbnailBar, &ThumbnailBar::indexActivated,
            this, &MainWindow::onThumbnailActivated);
    connect(m_thumbnailBar, &ThumbnailBar::indexAddToWorkspace,
            this, &MainWindow::onThumbnailAddToWorkspace);
    connect(m_thumbnailBar, &ThumbnailBar::workspaceSelectionChanged,
            this, &MainWindow::onThumbnailWorkspaceSelectionChanged);
    connect(m_thumbnailBar, &ThumbnailBar::removeIndicesRequested,
            this, &MainWindow::removeSessionIndices);
    connect(m_thumbnailBar, &ThumbnailBar::loadsChanged,
            this, &MainWindow::updateStatus);
    connect(m_imageView, &ImageView::workspacePathsChanged,
            this, &MainWindow::onWorkspacePathsChanged);
    connect(m_imageView, &ImageView::canvasSelectionChanged, this, [this]() {
        // Let the tile selection paint first; action enablement can wait a tick.
        if (isGalleryMode()) {
            QTimer::singleShot(0, this, [this]() {
                if (!isGalleryMode()) {
                    return;
                }
                updateNavigationActions();
                // Mirror Gallery multi-select onto the filmstrip (same as Workspace).
                if (!m_syncingSelection && m_thumbnailBar && m_imageView) {
                    m_syncingSelection = true;
                    m_thumbnailBar->setSelectedIndices(m_imageView->selectedSessionIndices());
                    m_syncingSelection = false;
                }
            });
            return;
        }
        updateNavigationActions();
        if (isWorkspaceMode()) {
            updateWorkspaceActionVisibility();
            updateLayoutPanel();
        }
        if (m_syncingSelection || !isWorkspaceMode() || !m_thumbnailBar || !m_imageView) {
            return;
        }
        m_syncingSelection = true;
        m_thumbnailBar->setSelectedIndices(m_imageView->selectedSessionIndices());
        m_syncingSelection = false;
    });

    m_imageView->setMinimumHeight(120);
    setCentralWidget(m_imageView);

    m_thumbnailDock = new QDockWidget(tr("Filmstrip"), this);
    m_thumbnailDock->setObjectName(QStringLiteral("ThumbnailDock"));
    m_thumbnailDock->setWidget(m_thumbnailBar);
    m_thumbnailDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea
                                     | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    m_thumbnailDock->setFeatures(QDockWidget::DockWidgetClosable
                                 | QDockWidget::DockWidgetMovable
                                 | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::BottomDockWidgetArea, m_thumbnailDock);
    connect(m_thumbnailDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_toggleThumbnailBarAct && m_toggleThumbnailBarAct->isChecked() != visible) {
            m_toggleThumbnailBarAct->setChecked(visible);
        }
        if (!isFullScreen()) {
            m_thumbnailBarVisibleBeforeFullscreen = visible;
        }
    });
    connect(m_thumbnailDock, &QDockWidget::dockLocationChanged, this,
            &MainWindow::onThumbnailDockLocationChanged);

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
    // When the user opens the panel, load metadata for the current selection
    // (selection itself skips decode while the dock is hidden).
    connect(m_metadataDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && m_metadataPanel) {
            m_metadataPath.clear();
            updateMetadataPanel();
        }
    });

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
    connect(m_adjustmentsDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateAdjustmentsPanel();
        }
    });
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

    m_tocPanel = new TocPanel(this);
    m_tocDock = new QDockWidget(tr("Contents"), this);
    m_tocDock->setObjectName(QStringLiteral("TocDock"));
    m_tocDock->setWidget(m_tocPanel);
    m_tocDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    m_tocDock->setFeatures(QDockWidget::DockWidgetClosable
                           | QDockWidget::DockWidgetMovable
                           | QDockWidget::DockWidgetFloatable);
    addDockWidget(Qt::LeftDockWidgetArea, m_tocDock);
    m_tocDock->hide();
    connect(m_tocPanel, &TocPanel::navigateToPage, this, &MainWindow::navigateDocumentPage);
    connect(m_tocPanel, &TocPanel::openExternalUri, this, &MainWindow::openDocumentLinkUri);
    connect(m_tocDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) {
            updateTocPanel();
        }
    });

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
    escShortcut->setContext(Qt::WindowShortcut);
    connect(escShortcut, &QShortcut::activated, this, [this]() {
        // Prefer focusWidget ancestry: hasFocus() can be false for clear-button
        // children or right after ShortcutOverride races.
        auto focusIn = [](QWidget *root) -> bool {
            if (!root || !root->isVisible()) {
                return false;
            }
            QWidget *f = QApplication::focusWidget();
            return f && (f == root || root->isAncestorOf(f));
        };
        if (focusIn(m_locationBar) || (m_locationEdit && m_locationEdit->hasFocus())) {
            cancelLocationBar();
            return;
        }
        if (focusIn(m_searchBar) || (m_searchEdit && m_searchEdit->hasFocus())) {
            cancelSearchBar();
            return;
        }
        if (m_imageView && m_imageView->isCropMode()) {
            m_imageView->cancelCrop();
            return;
        }
        // Leave slideshow (playing or paused) before leaving fullscreen / Image mode.
        if (isSlideshowSession()) {
            stopSlideshow();
            return;
        }
        if (isFullScreen()) {
            showNormal();
            return;
        }
        // Image mode with a session always has Up (Workspace or implicit Gallery).
        if (m_imageView && m_imageView->isImageMode() && !m_session.paths().isEmpty()) {
            returnFromImageMode();
        }
    });

    // Dedicated F/F11 shortcuts (WindowShortcut) so leave-fullscreen is
    // reliable even when the checkable action and window state briefly disagree.
    // Window-scoped so a second MainWindow does not fight for the same keys.
    for (const int key : {static_cast<int>(Qt::Key_F), static_cast<int>(Qt::Key_F11)}) {
        auto *sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WindowShortcut);
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
    // Repeating clock tick. Scheduling is pure from elapsed time — no
    // single-shot arm/resume chains (those raced when interval == transition).
    m_slideshowTimer->setSingleShot(false);
    m_slideshowTimer->setInterval(16);
    connect(m_slideshowTimer, &QTimer::timeout, this, &MainWindow::onSlideshowTick);
    if (m_imageView) {
        connect(m_imageView, &ImageView::slideshowLiveTransitionFinished, this, [this]() {
            // Commit the target index for this transition. Scheduling of the
            // *next* transition stays with the pure clock only.
            const int to = m_slideshowPendingToIndex;
            m_slideshowPendingToIndex = -1;
            m_slideshowAdvancing = true;
            if (to >= 0 && to < m_session.paths().size() && m_currentIndex != to) {
                setCurrentIndex(to);
            }
            m_slideshowAdvancing = false;
        });
        // Dwell-resume must not schedule — clock owns the schedule.
        connect(m_imageView, &ImageView::slideshowDwellResumeRequested, this, []() {});
    }

    m_cursorHideTimer = new QTimer(this);
    m_cursorHideTimer->setSingleShot(true);
    m_cursorHideTimer->setInterval(1000);
    connect(m_cursorHideTimer, &QTimer::timeout, this, &MainWindow::hideSlideshowCursor);

    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(false);
    } else {
        m_thumbnailBar->setVisible(false);
    }
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
    // Workspace multi-select and Gallery (when multi is enabled) share the same
    // filmstrip → canvas selection path by session index.
    if (!isWorkspaceMode() && !isGalleryMode()) {
        return;
    }
    const QList<int> sel = m_thumbnailBar->selectedIndices();
    if (!sel.isEmpty()) {
        const int idx = sel.last();
        if (idx != m_currentIndex && idx >= 0 && idx < m_session.paths().size()) {
            m_currentIndex = idx;
            // Invalidate so updateStatus → updateMetadataPanel refreshes when visible.
            if (m_metadataPanel) {
                m_metadataPath.clear();
            }
        }
    }
    // Shared selection: filmstrip drives canvas selection by session slot.
    if (!m_syncingSelection && m_imageView) {
        m_syncingSelection = true;
        m_imageView->selectBySessionIndices(sel);
        m_syncingSelection = false;
        if (isGalleryMode() && !sel.isEmpty() && sel.last() >= 0
            && sel.last() < m_session.paths().size()) {
            m_imageView->revealGalleryPath(m_session.paths().at(sel.last()));
        }
    }
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
        // Paste / drop still decoding: a PendingSessionBind owns the next id.
        // Allocating here created phantom filmstrip rows that could not drag.
        if (m_imageView->hasPendingSessionBindForPath(path)) {
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

void MainWindow::resetContentAppearance()
{
    if (!m_imageView || !m_imageView->targetHasContentAppearance()) {
        return;
    }
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Reset content appearance?"));
    box.setText(tr("Discard flip, rotation, and crop for the selected image(s)?"));
    box.setInformativeText(
        tr("The original files are never modified. This clears the local "
           "orientation saved for those files and restores the on-disk pixels "
           "in this session."));
    QPushButton *resetBtn = box.addButton(tr("Reset"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(cancelBtn);
    box.exec();
    if (box.clickedButton() != resetBtn) {
        return;
    }
    const int n = m_imageView->resetContentAppearanceForTargets();
    if (n > 0) {
        updateStatus();
        updateMetadataPanel();
        if (statusBar()) {
            statusBar()->showMessage(
                tr("Content appearance reset for %n image(s)", "", n), 4000);
        }
    }
}

void MainWindow::toggleCropMode()
{
    if (!m_imageView) {
        return;
    }
    if (m_imageView->isAttentionMode()) {
        m_imageView->setAttentionMode(false);
        if (m_attentionAct) {
            m_attentionAct->setChecked(false);
        }
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


void MainWindow::findOnPage()
{
    // Legacy entry point — same as Ctrl+F.
    openSearchBar();
}

void MainWindow::exportDocumentText()
{
    const QStringList pages = documentPagePathsForSearch();
    if (pages.isEmpty()) {
        if (statusBar()) {
            statusBar()->showMessage(tr("No document pages to export text from"), 5000);
        }
        return;
    }
    QString suggested = QStringLiteral("document.txt");
    if (m_imageView) {
        const QString path = m_imageView->classicPath();
        if (!path.isEmpty()) {
            const QString doc = PagePath::documentFilePath(path);
            if (!doc.isEmpty()) {
                suggested = QFileInfo(doc).completeBaseName() + QStringLiteral(".txt");
            }
        }
    }
    const QString outPath = QFileDialog::getSaveFileName(
        this, tr("Export Text"), suggested, tr("Text files (*.txt);;All files (*)"));
    if (outPath.isEmpty()) {
        return;
    }
    if (statusBar()) {
        statusBar()->showMessage(tr("Exporting text from %n page(s)…", "", pages.size()), 0);
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QStringList blocks;
    blocks.reserve(pages.size());
    int pagesWithText = 0;
    int totalRegions = 0;
    for (const QString &pagePath : pages) {
        ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(pagePath);
        if (layer.regions.isEmpty()) {
            layer = ThumtooCache::ensurePageTextLayer(pagePath);
        }
        QStringList lines;
        for (const ThumtooCache::TextRegion &r : layer.regions) {
            if (r.role != ThumtooCache::TextRegion::Role::Text || r.text.isEmpty()) {
                continue;
            }
            lines.append(r.text);
            ++totalRegions;
        }
        if (!lines.isEmpty()) {
            ++pagesWithText;
            const int pageNo = PagePath::pageNumber(pagePath);
            blocks.append(QStringLiteral("----- page %1 -----").arg(pageNo));
            blocks.append(lines.join(QLatin1Char('\n')));
            blocks.append(QString());
        }
    }
    QApplication::restoreOverrideCursor();
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export Text"),
                             tr("Could not write %1").arg(outPath));
        return;
    }
    // UTF-8 with BOM — helps Windows Notepad; Linux editors ignore it.
    const QByteArray utf8 = blocks.join(QLatin1Char('\n')).toUtf8();
    static const char bom[] = "\xEF\xBB\xBF";
    f.write(bom, 3);
    f.write(utf8);
    f.close();
    qWarning().noquote()
        << QStringLiteral("[find] exportText path=%1 pages=%2 withText=%3 regions=%4 bytes=%5")
               .arg(outPath)
               .arg(pages.size())
               .arg(pagesWithText)
               .arg(totalRegions)
               .arg(utf8.size());
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Exported text from %1/%2 pages (%3 regions) → %4")
                .arg(pagesWithText)
                .arg(pages.size())
                .arg(totalRegions)
                .arg(QFileInfo(outPath).fileName()),
            8000);
    }
}


void MainWindow::openSearchBar()
{
    if (!m_searchBar || !m_searchEdit) {
        return;
    }
    if (m_imageView && m_searchEdit->text() != m_imageView->textSearchQuery()) {
        QSignalBlocker block(m_searchEdit);
        m_searchEdit->setText(m_imageView->textSearchQuery());
    }
    if (m_searchFuzzyCheck && m_imageView) {
        QSignalBlocker block(m_searchFuzzyCheck);
        m_searchFuzzyCheck->setChecked(m_imageView->textSearchFuzzy());
    }
    updateSearchMatchLabel();
    m_searchBar->setVisible(true);
    m_searchEdit->setFocus(Qt::ShortcutFocusReason);
    m_searchEdit->selectAll();
}

void MainWindow::cancelSearchBar()
{
    if (!m_searchBar || !m_searchEdit) {
        return;
    }
    m_searchEdit->clearFocus();
    if (!m_searchBarPinned) {
        m_searchBar->setVisible(false);
    }
}

void MainWindow::commitSearchBar()
{
    findNextMatch();
}

void MainWindow::setSearchBarPinned(bool pinned)
{
    m_searchBarPinned = pinned;
    if (m_showSearchBarAct && m_showSearchBarAct->isChecked() != pinned) {
        QSignalBlocker block(m_showSearchBarAct);
        m_showSearchBarAct->setChecked(pinned);
    }
    if (!m_searchBar) {
        return;
    }
    if (pinned) {
        m_searchBar->setVisible(true);
        if (m_searchEdit && m_imageView) {
            QSignalBlocker block(m_searchEdit);
            m_searchEdit->setText(m_imageView->textSearchQuery());
            updateSearchMatchLabel();
        }
    } else if (m_searchEdit && !m_searchEdit->hasFocus()) {
        m_searchBar->setVisible(false);
    }
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    if (!m_imageView) {
        return;
    }
    const bool fuzzy = !m_searchFuzzyCheck || m_searchFuzzyCheck->isChecked();
    m_imageView->setTextSearchFuzzy(fuzzy);
    const QString path = m_imageView->classicPath();
    m_docSearchPageMatchCount = m_imageView->setTextSearchQuery(text);
    m_docSearchHitPages.clear();
    m_docSearchHitIndex = -1;
    m_docSearchQuery = text.trimmed();
    updateSearchMatchLabel();
    qWarning().noquote()
        << QStringLiteral(
               "[find] query=%1 fuzzy=%2 path=%3 pageRef=%4 hasLayer=%5 regions=%6 matches=%7 mode=%8")
               .arg(text.trimmed())
               .arg(fuzzy)
               .arg(path)
               .arg(PagePath::isPageRef(path))
               .arg(m_imageView->hasTextLayer())
               .arg(m_imageView->textLayerRegionCount())
               .arg(m_docSearchPageMatchCount)
               .arg(m_imageView->isImageMode()
                        ? QStringLiteral("image")
                        : (m_imageView->isGalleryMode() ? QStringLiteral("gallery")
                                                        : QStringLiteral("workspace")));
    if (!text.trimmed().isEmpty() && statusBar()) {
        if (!PagePath::isPageRef(path)) {
            statusBar()->showMessage(
                tr("Find works on PDF / DjVu / EPUB pages"), 4000);
        } else if (!m_imageView->hasTextLayer()) {
            statusBar()->showMessage(
                tr("No extractable text on this page (scanned image?) — try File → Export Text"),
                5000);
        } else if (m_docSearchPageMatchCount == 0) {
            statusBar()->showMessage(
                tr("No matches on this page (%n text region(s))", "",
                   m_imageView->textLayerRegionCount()),
                3000);
        }
    }
    scheduleDocumentSearch(text);
}

void MainWindow::updateSearchMatchLabel()
{
    if (!m_searchMatchLabel) {
        return;
    }
    if (!m_searchEdit || m_searchEdit->text().trimmed().isEmpty()) {
        m_searchMatchLabel->clear();
        if (m_searchPrevBtn) {
            m_searchPrevBtn->setEnabled(false);
        }
        if (m_searchNextBtn) {
            m_searchNextBtn->setEnabled(false);
        }
        return;
    }
    const int pageHits = m_docSearchPageMatchCount;
    const int pagesWith = m_docSearchHitPages.size();
    QString text;
    if (m_docSearchRunning) {
        text = tr("Searching…");
        if (pageHits > 0) {
            text += tr(" · %n on page", "", pageHits);
        }
    } else if (pagesWith > 0) {
        const int cur = m_docSearchHitIndex >= 0 ? m_docSearchHitIndex + 1 : 0;
        text = tr("%1/%2 pages").arg(cur).arg(pagesWith);
        if (pageHits > 0) {
            text += tr(" · %n on page", "", pageHits);
        }
    } else if (pageHits > 0) {
        text = tr("%n on page", "", pageHits);
    } else if (m_imageView && !m_imageView->hasTextLayer()
               && PagePath::isPageRef(m_imageView->classicPath())) {
        text = tr("No text");
    } else if (m_imageView && !PagePath::isPageRef(m_imageView->classicPath())) {
        text = tr("Not a document page");
    } else {
        text = tr("No matches");
    }
    m_searchMatchLabel->setText(text);
    const bool nav = pagesWith > 0 || pageHits > 0;
    if (m_searchPrevBtn) {
        m_searchPrevBtn->setEnabled(nav);
    }
    if (m_searchNextBtn) {
        m_searchNextBtn->setEnabled(nav);
    }
}

QStringList MainWindow::documentPagePathsForSearch() const
{
    QStringList out;
    if (!m_imageView) {
        return out;
    }
    QString path = m_imageView->classicPath();
    if (path.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path.isEmpty() || !PagePath::isPageRef(path)) {
        return out;
    }
    const QString doc = PagePath::documentFilePath(path);
    if (doc.isEmpty()) {
        return out;
    }
    // Prefer already-expanded session rows for this document (stable paths).
    for (const QString &p : m_session.paths()) {
        if (PagePath::isPageRef(p) && PagePath::documentFilePath(p) == doc) {
            out.append(p);
        }
    }
    if (out.isEmpty() && PagePath::isPageRef(path)) {
        out.append(path);
    }
    if (!out.isEmpty()) {
        return out;
    }
    // Fall back to expand helpers.
    const QString layout = PagePath::epubLayoutParamsOf(path);
    if (!layout.isEmpty() || path.contains(QLatin1String("//epub:"))) {
        return ThumtooCache::expandEpubToPageRefs(doc);
    }
    if (PagePath::isEpubFile(doc)) {
        return ThumtooCache::expandEpubToPageRefs(doc);
    }
    // PDF / DjVu page refs share makeRef form; try both expanders.
    QStringList pdf = ThumtooCache::expandPdfToPageRefs(doc);
    if (!pdf.isEmpty()) {
        return pdf;
    }
    return ThumtooCache::expandDjvuToPageRefs(doc);
}

void MainWindow::scheduleDocumentSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        ++m_docSearchGeneration;
        m_docSearchRunning = false;
        m_docSearchHitPages.clear();
        m_docSearchHitIndex = -1;
        m_docSearchQuery.clear();
        updateSearchMatchLabel();
        return;
    }
    if (!m_docSearchDebounce) {
        m_docSearchDebounce = new QTimer(this);
        m_docSearchDebounce->setSingleShot(true);
        connect(m_docSearchDebounce, &QTimer::timeout, this, [this]() {
            startDocumentSearch(m_docSearchQuery);
        });
    }
    m_docSearchQuery = trimmed;
    m_docSearchDebounce->start(280);
}

void MainWindow::startDocumentSearch(const QString &query)
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    const QStringList pages = documentPagePathsForSearch();
    if (pages.isEmpty()) {
        // Single-page / non-document: page-local search only.
        m_docSearchRunning = false;
        m_docSearchHitPages.clear();
        updateSearchMatchLabel();
        return;
    }
    const quint64 gen = ++m_docSearchGeneration;
    m_docSearchRunning = true;
    updateSearchMatchLabel();

    const bool fuzzy = !m_searchFuzzyCheck || m_searchFuzzyCheck->isChecked();
    const QPointer<MainWindow> guard(this);
    QThreadPool::globalInstance()->start([guard, pages, trimmed, fuzzy, gen]() {
        QVector<int> hitPages;
        hitPages.reserve(64);
        for (const QString &pagePath : pages) {
            if (!guard) {
                return;
            }
            ThumtooCache::PageTextLayer layer = ThumtooCache::cachedPageTextLayer(pagePath);
            if (layer.regions.isEmpty()) {
                layer = ThumtooCache::ensurePageTextLayer(pagePath);
            }
            int matches = 0;
            for (const ThumtooCache::TextRegion &r : layer.regions) {
                if (r.text.isEmpty()) {
                    continue;
                }
                if (ImageView::textMatchesQuery(r.text, trimmed, fuzzy)) {
                    ++matches;
                }
            }
            if (matches > 0) {
                const int page = PagePath::pageNumber(pagePath);
                if (page > 0) {
                    hitPages.append(page);
                }
            }
        }
        if (!guard) {
            return;
        }
        // Generation checked only on the GUI thread (no data race on the counter).
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, gen, trimmed, hitPages]() {
                MainWindow *host = guard.data();
                if (!host || gen != host->m_docSearchGeneration) {
                    return;
                }
                host->onDocumentSearchFinished(gen, trimmed, hitPages, 0);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::onDocumentSearchFinished(quint64 generation, const QString &query,
                                          const QVector<int> &hitPages, int /*pageHits*/)
{
    if (generation != m_docSearchGeneration) {
        return;
    }
    m_docSearchRunning = false;
    m_docSearchQuery = query;
    m_docSearchHitPages = hitPages;
    // Point at the hit for the current page when possible.
    m_docSearchHitIndex = -1;
    if (m_imageView && !hitPages.isEmpty()) {
        const int cur = PagePath::pageNumber(m_imageView->classicPath());
        for (int i = 0; i < hitPages.size(); ++i) {
            if (hitPages.at(i) == cur) {
                m_docSearchHitIndex = i;
                break;
            }
        }
        if (m_docSearchHitIndex < 0) {
            m_docSearchHitIndex = 0;
        }
    }
    if (m_imageView) {
        m_docSearchPageMatchCount = m_imageView->textSearchMatchCount();
    }
    updateSearchMatchLabel();
    qWarning().noquote()
        << QStringLiteral("[find] docScan done query=%1 pagesWithHits=%2 pageMatches=%3")
               .arg(query)
               .arg(hitPages.size())
               .arg(m_docSearchPageMatchCount);
    if (statusBar()) {
        if (hitPages.isEmpty()) {
            statusBar()->showMessage(tr("No matches in document"), 3000);
        } else {
            statusBar()->showMessage(
                tr("%n page(s) with matches", "", hitPages.size()), 3000);
        }
    }
}

void MainWindow::goToSearchHit(int index)
{
    if (index < 0 || index >= m_docSearchHitPages.size()) {
        return;
    }
    m_docSearchHitIndex = index;
    const int page = m_docSearchHitPages.at(index);
    navigateDocumentPage(page);
    // Re-apply query so highlights load for the new page.
    if (m_imageView && !m_docSearchQuery.isEmpty()) {
        m_docSearchPageMatchCount = m_imageView->setTextSearchQuery(m_docSearchQuery);
    }
    updateSearchMatchLabel();
}

void MainWindow::findNextMatch()
{
    if (m_docSearchHitPages.isEmpty()) {
        // Only page-local matches — nothing to navigate.
        if (m_docSearchPageMatchCount > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("%n match(es) on this page", "", m_docSearchPageMatchCount), 2000);
        }
        return;
    }
    int next = m_docSearchHitIndex + 1;
    if (next >= m_docSearchHitPages.size()) {
        next = 0;
    }
    goToSearchHit(next);
}

void MainWindow::findPreviousMatch()
{
    if (m_docSearchHitPages.isEmpty()) {
        return;
    }
    int prev = m_docSearchHitIndex - 1;
    if (prev < 0) {
        prev = m_docSearchHitPages.size() - 1;
    }
    goToSearchHit(prev);
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


void MainWindow::toggleAttentionMode()
{
    if (!m_imageView || !m_attentionAct) {
        return;
    }
    const bool want = m_attentionAct->isChecked();
    if (want && m_imageView->isCropMode()) {
        m_imageView->cancelCrop();
        if (m_cropAct) {
            m_cropAct->setChecked(false);
        }
    }
    if (want && !m_imageView->isImageMode()) {
        // Attention edit is Image-mode only for now.
        if (m_session.paths().isEmpty()) {
            m_attentionAct->setChecked(false);
            return;
        }
        int idx = m_currentIndex;
        if (idx < 0 || idx >= m_session.paths().size()) {
            idx = 0;
        }
        openSessionIndexInImageMode(idx);
    }
    m_imageView->setAttentionMode(want);
    m_attentionAct->setChecked(m_imageView->isAttentionMode());
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
    if (m_session.paths().size() > 1
        && !(m_thumbnailDock ? m_thumbnailDock->isVisible() : m_thumbnailBar->isVisible())) {
        m_toggleThumbnailBarAct->setChecked(true);
        if (m_thumbnailDock) {
            m_thumbnailDock->setVisible(true);
        } else {
            m_thumbnailBar->setVisible(true);
        }
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
    if (isVisible()) {
        window->move(frameGeometry().topLeft() + QPoint(32, 32));
    }
    window->show();
    window->loadFiles(paths);
}

void MainWindow::newWindow()
{
    auto *window = new MainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    // Offset so the new frame is not fully stacked under this one.
    if (isVisible()) {
        window->move(frameGeometry().topLeft() + QPoint(32, 32));
    }
    window->show();
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
        // Workspace opens with nothing selected (filmstrip + canvas). Residual
        // Image/Gallery selection used to stay selected so a one-thumb drag
        // shipped every selected path via selectedItems().
        if (m_imageView->canvasScene()) {
            m_imageView->canvasScene()->clearSelection();
        }
        m_thumbnailBar->selectNoneThumbs();
        // Workspace starts/stays empty unless the user (or a project) places tiles.
        // Do not seed from the session list — arrangement is permanent across modes.
        syncThumbnailCanvasMembership();
        // Thumbnail visibility for Workspace is applied in updateWorkspaceActionVisibility
        // via updateThumbnailBarForMode (default preferred on).
        // Uncheck gallery layout actions
        for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                             m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                             m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct,
                             m_layoutFlowAct, m_layoutFlowFillAct, m_layoutFacingAct}) {
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

void MainWindow::setZoomTool()
{
    m_imageView->setTool(ImageView::Tool::Zoom);
    if (m_zoomToolAct) {
        m_zoomToolAct->setChecked(true);
    }
}


void MainWindow::showSlideshowSettings()
{
    SlideshowSettingsDialog dlg(this);
    dlg.setIntervalMs(m_slideshowIntervalMs);
    dlg.setStartFullscreen(m_slideshowFullscreen);
    if (m_imageView) {
        dlg.setTransitionIndex(static_cast<int>(m_imageView->slideshowTransition()));
        dlg.setTransitionDurationMs(m_imageView->slideshowTransitionDurationMs());
        dlg.setMotionIndex(static_cast<int>(m_imageView->slideshowMotion()));
        dlg.setPanZoomFactor(m_imageView->panZoomFactor());
        dlg.setZoomIndex(static_cast<int>(m_imageView->slideshowZoom()));
        dlg.setLetterboxFillIndex(static_cast<int>(m_imageView->slideshowLetterboxFill()));
        dlg.setPadColor(m_imageView->slideshowPadColor());
        // Solid mode stores its own colour; surface current effective pad for UI.
        if (m_imageView->slideshowLetterboxFill()
            == ImageView::SlideshowLetterboxFill::Solid) {
            // pad from setter path uses dedicated colour via slideshowPadColor
        }
    }
    // Live apply — no OK; Close dismisses. settingsChanged fires on each edit.
    auto applyFromDialog = [this, &dlg]() {
        setSlideshowIntervalMs(dlg.intervalMs());
        m_slideshowFullscreen = dlg.startFullscreen();
        if (!m_imageView) {
            return;
        }
        m_imageView->setSlideshowTransition(
            static_cast<ImageView::SlideshowTransition>(dlg.transitionIndex()));
        // Cap already enforced by the dialog max; clamp again for safety.
        // Duration is the full transition (out + in), so cap at the interval.
        const int intervalCap = m_slideshowIntervalMs;
        m_imageView->setSlideshowTransitionDurationMs(
            qMin(dlg.transitionDurationMs(), qMax(0, intervalCap)));
        m_imageView->setSlideshowMotion(
            static_cast<ImageView::SlideshowMotion>(dlg.motionIndex()));
        m_imageView->setPanZoomFactor(dlg.panZoomFactor());
        m_imageView->setSlideshowZoom(
            static_cast<ImageView::SlideshowZoom>(qBound(0, dlg.zoomIndex(), 2)));
        m_imageView->setSlideshowPadColor(dlg.padColor());
        m_imageView->setSlideshowLetterboxFill(
            static_cast<ImageView::SlideshowLetterboxFill>(
                qBound(0, dlg.letterboxFillIndex(), 2)));
        // Interval changes remap phase inside setSlideshowIntervalMs. Other live
        // settings must not re-arm the clock (that restarted the dwell at 0 and
        // felt like a long pause on the current image).
        if (m_slideshowClockRunning && !m_slideshowPaused) {
            m_imageView->cancelSlideshowTransition();
            m_slideshowPendingToIndex = -1;
            m_slideshowTransitionCycle = -1;
            m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
            m_imageView->reapplySlideshowFraming();
            updateSlideshowFromClock();
        }
        writeSettings();
    };
    connect(&dlg, &SlideshowSettingsDialog::settingsChanged, this, applyFromDialog);
    dlg.exec();
}

void MainWindow::remapSlideshowPhase(int oldIntervalMs, int newIntervalMs)
{
    // Map elapsed under oldInterval → same cycle index + normalized phase under
    // newInterval. Keeps the visible slide; avoids a full dwell restart when
    // the user edits interval while the show is running or paused.
    if (oldIntervalMs <= 0) {
        oldIntervalMs = 1;
    }
    if (newIntervalMs <= 0) {
        newIntervalMs = 1;
    }
    const int n = m_session.paths().size();
    if (n <= 0 || !m_slideshowClockRunning) {
        return;
    }

    qint64 elapsed = m_slideshowPausedAccumMs;
    if (!m_slideshowPaused && m_slideshowClock.isValid()) {
        elapsed += m_slideshowClock.elapsed();
    }
    if (elapsed < 0) {
        elapsed = 0;
    }

    const qint64 cycle = elapsed / oldIntervalMs;
    const qreal phaseT = qreal(elapsed % oldIntervalMs) / qreal(oldIntervalMs);

    m_slideshowBaseIndex = int((qint64(m_slideshowBaseIndex) + cycle) % n);
    // Stay strictly inside the cycle so we do not land on a sticky "done" edge.
    const qint64 newPhase =
        qBound(qint64(0), qint64(qRound(phaseT * qreal(newIntervalMs))),
               qint64(newIntervalMs) - 1);
    m_slideshowPausedAccumMs = newPhase;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    if (!m_slideshowPaused) {
        m_slideshowClock.start();
    }
}

void MainWindow::armSlideshowAdvanceTimer()
{
    // Restart the pure clock at the current session index (start / resume /
    // ←/→ navigation). Interval *edits* use remapSlideshowPhase instead so the
    // dwell is not reset to zero.
    if (m_slideshowPaused || !m_slideshowTimer) {
        return;
    }
    if (m_session.paths().size() <= 1 || isWorkspaceMode()) {
        return;
    }

    m_slideshowBaseIndex = qBound(0, m_currentIndex, m_session.paths().size() - 1);
    m_slideshowPausedAccumMs = 0;
    m_slideshowTransitionCycle = -1;
    m_slideshowPendingToIndex = -1;
    m_slideshowClock.start();
    m_slideshowClockRunning = true;

    if (m_imageView) {
        m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
        m_imageView->reapplySlideshowFraming();
        if (m_session.paths().size() > 1) {
            int next = (m_currentIndex + 1) % m_session.paths().size();
            if (next < 0) {
                next = 0;
            }
            m_imageView->preloadSlideshowImage(m_session.paths().at(next));
        }
    }

    if (!m_slideshowTimer->isActive()) {
        m_slideshowTimer->start();
    }
    updateSlideshowFromClock();
}

void MainWindow::updateSlideshowFromClock()
{
    // ------------------------------------------------------------------
    // Pure scheduler: wall clock is the only authority for *when* and
    // *which* pair (fromIdx → toIdx). ImageView only renders a transition
    // when asked; its finished signal installs the session index but does
    // not schedule the next step.
    //
    // elapsed → cycle → (fromIdx, toIdx, phase)
    // phase < pureMs  → show fromIdx only
    // phase ≥ pureMs  → one transition fromIdx→toIdx per cycle
    // ------------------------------------------------------------------
    if (m_slideshowPaused || !m_slideshowClockRunning || !m_slideshowTimer) {
        return;
    }
    const int n = m_session.paths().size();
    if (n <= 1 || isWorkspaceMode()) {
        return;
    }

    int intervalMs = m_slideshowIntervalMs;
    if (intervalMs <= 0) {
        intervalMs = 1;
    }
    int transitionMs = m_imageView ? m_imageView->slideshowTransitionDurationMs() : 0;
    transitionMs = qBound(0, transitionMs, intervalMs);
    const int pureMs = intervalMs - transitionMs;

    const qint64 elapsed = m_slideshowPausedAccumMs + m_slideshowClock.elapsed();
    const qint64 cycle = elapsed / intervalMs;
    const int phaseMs = int(elapsed % intervalMs);
    const int fromIdx = int((qint64(m_slideshowBaseIndex) + cycle) % n);
    const int toIdx = (fromIdx + 1) % n;

    // HUD: session loop position (video-player style).
    if (m_imageView) {
        const qint64 totalMs = qint64(n) * qint64(intervalMs);
        const qint64 raw =
            qint64(m_slideshowBaseIndex) * qint64(intervalMs) + elapsed;
        m_imageView->setSlideshowTimeline(totalMs > 0 ? (raw % totalMs) : 0, totalMs);
        // Full decodes for the visible pair (and one look-ahead). Pure phase
        // keeps fulls in m_ssFullByPath so A and B can both be full at once.
        // Do not skip toIdx during the transition — that left only soft paint.
        m_imageView->preloadSlideshowImage(m_session.paths().at(fromIdx));
        m_imageView->preloadSlideshowImage(m_session.paths().at(toIdx));
        m_imageView->preloadSlideshowImage(m_session.paths().at((toIdx + 1) % n));
    }

    // Pure phase drive — no beginLive / busy / cancel for Crossfade.
    // Every tick: set buffers + fadeT from wall arithmetic; blit does the rest.
    if (m_imageView) {
        m_imageView->setSlideshowProgress(true, intervalMs);
    }

    const QString fromPath = m_session.paths().at(fromIdx);
    const QString toPath = m_session.paths().at(toIdx);

    if (phaseMs < pureMs || transitionMs <= 0) {
        if (m_imageView) {
            m_imageView->setSlideshowPhase(fromPath, QString(), -1.0);
        }
        if (m_currentIndex != fromIdx && !m_slideshowAdvancing) {
            m_slideshowAdvancing = true;
            setCurrentIndex(fromIdx);
            m_slideshowAdvancing = false;
        }
        m_slideshowPendingToIndex = -1;
        m_slideshowTransitionCycle = -1;
        return;
    }

    // Transition window: t from pure clock only (Crossfade + FadeBlack).
    const qreal t = qBound(0.0, qreal(phaseMs - pureMs) / qreal(transitionMs), 1.0);
    if (m_imageView) {
        const auto tr = m_imageView->slideshowTransition();
        if (tr == ImageView::SlideshowTransition::Crossfade
            || tr == ImageView::SlideshowTransition::FadeBlack
            || tr == ImageView::SlideshowTransition::Slide) {
            if (m_slideshowTransitionCycle != cycle) {
                qCDebug(lcSlideshow).nospace()
                    << "[slideshow] phase-fade cycle=" << cycle
                    << " phase=" << phaseMs
                    << " t=" << QString::number(t, 'f', 3)
                    << " from=" << fromIdx
                    << " to=" << toIdx
                    << " path=" << QFileInfo(toPath).fileName();
                m_slideshowTransitionCycle = cycle;
                m_slideshowPendingToIndex = toIdx;
            }
            m_imageView->setSlideshowPhase(fromPath, toPath, t);
            // phaseMs ∈ [0, intervalMs). When pureMs==0, t never reaches 1.0
            // because phase max is interval-1. Commit on the last phase sample.
            const bool transitionDone =
                (t >= 1.0 - 1e-6) || (phaseMs >= intervalMs - 1);
            if (transitionDone && m_currentIndex != toIdx && !m_slideshowAdvancing) {
                m_slideshowAdvancing = true;
                setCurrentIndex(toIdx);
                m_slideshowAdvancing = false;
                m_slideshowPendingToIndex = -1;
            }
            return;
        }
    }

    // Non-crossfade transitions: keep legacy once-per-cycle path.
    if (m_slideshowTransitionCycle == cycle) {
        return;
    }
    if (m_currentIndex != fromIdx) {
        if (!m_slideshowAdvancing) {
            m_slideshowAdvancing = true;
            setCurrentIndex(fromIdx);
            m_slideshowAdvancing = false;
        }
        return;
    }
    m_slideshowTransitionCycle = cycle;
    m_slideshowPendingToIndex = -1;
    m_slideshowAdvancing = true;
    if (m_imageView && m_imageView->isImageMode()) {
        m_imageView->prepareSlideshowTransition();
    }
    setCurrentIndex(toIdx);
    m_slideshowAdvancing = false;
}

void MainWindow::onSlideshowTick()
{
    updateSlideshowFromClock();
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
    if (m_thumbnailDock) {
        m_thumbnailDock->setVisible(visible);
    } else if (m_thumbnailBar) {
        m_thumbnailBar->setVisible(visible);
    }
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
    if (!m_thumbnailBar || !m_thumbnailDock) {
        m_thumbnailEdge = edge;
        return;
    }
    m_thumbnailEdge = edge;
    const bool horizontalBar =
        (edge == ThumbnailEdge::Bottom || edge == ThumbnailEdge::Top);
    const Qt::Orientation barOrientation =
        horizontalBar ? Qt::Horizontal : Qt::Vertical;

    m_dockLocationGuard = true;
    m_thumbnailBar->setBarOrientation(barOrientation);

    Qt::DockWidgetArea area = Qt::BottomDockWidgetArea;
    switch (edge) {
    case ThumbnailEdge::Top:
        area = Qt::TopDockWidgetArea;
        break;
    case ThumbnailEdge::Left:
        area = Qt::LeftDockWidgetArea;
        break;
    case ThumbnailEdge::Right:
        area = Qt::RightDockWidgetArea;
        break;
    case ThumbnailEdge::Bottom:
    default:
        area = Qt::BottomDockWidgetArea;
        break;
    }
    addDockWidget(area, m_thumbnailDock);

    const int thumbSize = m_thumbnailBar->thumbSize();
    const int barExtent = ThumbnailBar::extentForThumbSize(thumbSize);
    // Let the user resize the dock; seed a sensible default extent.
    if (horizontalBar) {
        m_thumbnailBar->setMinimumHeight(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMinThumbSize));
        m_thumbnailBar->setMaximumHeight(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMaxThumbSize));
        m_thumbnailBar->setMinimumWidth(0);
        m_thumbnailBar->setMaximumWidth(QWIDGETSIZE_MAX);
        resizeDocks({m_thumbnailDock}, {barExtent}, Qt::Vertical);
    } else {
        m_thumbnailBar->setMinimumWidth(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMinThumbSize));
        m_thumbnailBar->setMaximumWidth(ThumbnailBar::extentForThumbSize(ThumbnailBar::kMaxThumbSize));
        m_thumbnailBar->setMinimumHeight(0);
        m_thumbnailBar->setMaximumHeight(QWIDGETSIZE_MAX);
        resizeDocks({m_thumbnailDock}, {barExtent}, Qt::Horizontal);
    }
    m_thumbnailBar->setThumbSize(thumbSize);
    m_dockLocationGuard = false;
    updateThumbnailEdgeActions();
}

void MainWindow::onThumbnailDockLocationChanged(Qt::DockWidgetArea area)
{
    if (m_dockLocationGuard || !m_thumbnailBar) {
        return;
    }
    ThumbnailEdge edge = m_thumbnailEdge;
    switch (area) {
    case Qt::LeftDockWidgetArea:
        edge = ThumbnailEdge::Left;
        break;
    case Qt::RightDockWidgetArea:
        edge = ThumbnailEdge::Right;
        break;
    case Qt::TopDockWidgetArea:
        edge = ThumbnailEdge::Top;
        break;
    case Qt::BottomDockWidgetArea:
        edge = ThumbnailEdge::Bottom;
        break;
    default:
        // Floating: keep last edge / orientation.
        return;
    }
    if (edge == m_thumbnailEdge) {
        return;
    }
    m_thumbnailEdge = edge;
    const bool horizontalBar =
        (edge == ThumbnailEdge::Bottom || edge == ThumbnailEdge::Top);
    m_thumbnailBar->setBarOrientation(horizontalBar ? Qt::Horizontal : Qt::Vertical);
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
        "Click centre — pause / resume slideshow<br/>"
        "Home / End — first / last<br/>"
        "Space — start/stop slideshow<br/>"
        "[ / ] — slower / faster slideshow (dwell interval)<br/>"
        "Esc — leave fullscreen (or return to Gallery)</p>"
        "<p><b>Slideshow</b><br/>"
        "Preferences: transition (none, crossfade, fade to black, slide), "
        "duration, and optional pan&zoom or pan&scan during each dwell. "
        "Transitions apply on automatic advances only.</p>"
        "<p><b>View</b><br/>"
        "Ctrl+Shift+N — new window<br/>"
        "F / F11 — fullscreen (chrome and docks hide; restored on exit)<br/>"
        "H — toggle HUD (filename / session index; dwell progress while slideshow runs)<br/>"
        "F5 — reload from disk (current image / gallery tiles)<br/>"
        "Ctrl+0 — zoom 1:1 · Ctrl++ / Ctrl+- — zoom<br/>"
        "Ctrl+F — find · Ctrl+Shift+0 — fill · Fit — fit to window · Z — zoom region<br/>"
        "Ctrl+T — toolbar · Ctrl+M — thumbnails · Ctrl+E — metadata<br/>"
        "Ctrl+U — colour adjustments · F1 — this list</p>"
        "<p><b>Image</b><br/>"
        "&lt; / &gt; — rotate left / right · R / Ctrl+R — rotate right · Ctrl+Shift+R — rotate left<br/>Ctrl+L — open location · F5 — reload<br/>"
        "Ctrl+H / Ctrl+Shift+H — flip horizontal / vertical<br/>"
        "C — crop mode</p>"
        "<p><b>Workspace</b><br/>"
        "Tools: Select, Pan, Zoom (drag region) on the left toolbar<br/>"
        "Ctrl+C / Ctrl+X / Ctrl+V — copy / cut / paste tiles<br/>"
        "Ctrl+D — duplicate · Delete — remove from canvas<br/>"
        "Ctrl+Shift+↑/↓ — raise / lower<br/>"
        "Ctrl+Shift+=/− — opacity up / down<br/>"
        "Alt+[ / Alt+] — nudge shear · Alt+0 — reset shear<br/>"
        "Group scale: edges H/V · corners uniform · Shift frees axes<br/>"
        "Page Guide + Fit to Content; Workspace Background (+ temporary Default)</p>"
        "<p><b>Gallery</b><br/>"
        "Click — select · Ctrl/Shift — multi-select · Double-click / Enter — open<br/>"
        "Arrow keys — spatial focus among tiles</p>"
        "<p><b>Files</b><br/>"
        "Ctrl+O — open · Ctrl+Shift+A — add · Ctrl+Shift+O — open project<br/>"
        "Ctrl+S / Ctrl+Shift+S — save / save project as<br/>"
        "Q / Ctrl+Q — quit</p>"));
    box.setStandardButtons(QMessageBox::Close);
    box.button(QMessageBox::Close)->setText(tr("&Close"));
    box.setDefaultButton(QMessageBox::Close);
    box.exec();
}

void MainWindow::about()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("About Biltoo"));
    box.setIconPixmap(QApplication::windowIcon().pixmap(64, 64));
    box.setText(tr("<h3>Biltoo %1</h3>").arg(QApplication::applicationVersion()));

    // Compile-time optional libs only (no runtime user-disable yet).
    auto feat = [](bool on) {
        return on ? QObject::tr("✔ enabled") : QObject::tr("✘ missing");
    };
    const QString features = tr(
        "<p><b>Optional features in this build</b></p>"
        "<ul>"
        "<li>libvips (extra codecs, attention Pan&amp;Zoom): %1</li>"
        "<li>libexiv2 (Exif / IPTC / XMP metadata): %2</li>"
        "<li>thumtoo archives (zip / tar / 7z / rar / …): %3</li>"
        "<li>GIO (default-application Preferences): %4</li>"
        "</ul>"
        "<p>Qt imageformat plugins (e.g. KImageFormats for XCF) are loaded at "
        "runtime when installed.</p>")
        .arg(feat(BILTOO_FEATURE_VIPS),
             feat(BILTOO_FEATURE_EXIV2),
             feat(BILTOO_FEATURE_ARCHIVE),
             feat(BILTOO_FEATURE_GIO));

    box.setInformativeText(
        tr("<p>A classic image viewer with Gallery overview and a free-form "
           "Workspace for comparing images.</p>"
           "%1"
           "<p>Copyright © 2026 Ingo Ruhnke &lt;grumbel@gmail.com&gt;<br/>"
           "License: GPL-3.0-or-later</p>")
            .arg(features));
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
    if (m_imageView) {
        dlg.setSlideshowTransitionIndex(static_cast<int>(m_imageView->slideshowTransition()));
        dlg.setSlideshowTransitionDurationMs(m_imageView->slideshowTransitionDurationMs());
        dlg.setSlideshowMotionIndex(static_cast<int>(m_imageView->slideshowMotion()));
        dlg.setPanZoomFactor(m_imageView->panZoomFactor());
        dlg.setSlideshowZoomIndex(static_cast<int>(m_imageView->slideshowZoom()));
        dlg.setSlideshowLetterboxFillIndex(static_cast<int>(m_imageView->slideshowLetterboxFill()));
        dlg.setSlideshowPadColor(m_imageView->slideshowPadColor());
    }
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
    if (m_imageView) {
        m_imageView->setSlideshowTransition(
            static_cast<ImageView::SlideshowTransition>(dlg.slideshowTransitionIndex()));
        {
            // Full transition (out + in); cap at the interval.
            const int intervalCap = m_slideshowIntervalMs;
            m_imageView->setSlideshowTransitionDurationMs(
                qMin(dlg.slideshowTransitionDurationMs(), qMax(0, intervalCap)));
        }
        m_imageView->setSlideshowMotion(
            static_cast<ImageView::SlideshowMotion>(dlg.slideshowMotionIndex()));
        m_imageView->setPanZoomFactor(dlg.panZoomFactor());
        m_imageView->setSlideshowZoom(
            static_cast<ImageView::SlideshowZoom>(
                qBound(0, dlg.slideshowZoomIndex(), 2)));
        m_imageView->setSlideshowPadColor(dlg.slideshowPadColor());
        m_imageView->setSlideshowLetterboxFill(
            static_cast<ImageView::SlideshowLetterboxFill>(
                qBound(0, dlg.slideshowLetterboxFillIndex(), 2)));
        // If a slideshow is running, re-frame the current slide for zoom/motion.
        if (m_slideshowClockRunning) {
            m_imageView->setSlideshowProgress(true, m_slideshowIntervalMs);
            m_imageView->reapplySlideshowFraming();
        }
    }
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
    if (m_thumbnailBar) {
        m_thumbnailBar->setStripBackground(dlg.backgroundColor());
    }
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
    if (m_session.paths().isEmpty()) {
        return;
    }
    // Gallery / Workspace: select every live canvas tile (Ctrl+A also handled
    // in ImageView). Filmstrip Select All remains the session multi-select when
    // focus is on the strip or we are in Image mode.
    if (m_imageView && (isGalleryMode() || isWorkspaceMode())) {
        m_imageView->selectAllCanvasItems();
        return;
    }
    if (!m_thumbnailBar) {
        return;
    }
    m_thumbnailBar->selectAllThumbs();
}

void MainWindow::updateWindowTitle()
{
    if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        const QString name = PagePath::displayName(m_session.paths().at(m_currentIndex));
        // GNOME-style: document name first, then app
        setWindowTitle(tr("%1 — Biltoo").arg(name));
    } else {
        setWindowTitle(tr("Biltoo"));
    }
}


void MainWindow::updateTocPanel()
{
    if (!m_tocPanel || !m_imageView) {
        return;
    }
    const QString path = m_imageView->classicPath();
    if (path.isEmpty() || (!PagePath::isPageRef(path) && !PagePath::isEpubLayoutOnly(path))) {
        // Try session path at current index
        QString sessionPath = path;
        if (sessionPath.isEmpty() && m_currentIndex >= 0
            && m_currentIndex < m_session.paths().size()) {
            sessionPath = m_session.paths().at(m_currentIndex);
        }
        if (sessionPath.isEmpty()
            || (!PagePath::isPageRef(sessionPath) && !PagePath::isEpubLayoutOnly(sessionPath)
                && !PagePath::isPdfFile(sessionPath) && !PagePath::isDjvuFile(sessionPath)
                && !PagePath::isEpubFile(sessionPath))) {
            m_tocPanel->clear();
            return;
        }
        const auto outline = ThumtooCache::ensureDocumentOutline(sessionPath);
        m_tocPanel->setOutline(outline);
        return;
    }
    const auto outline = ThumtooCache::ensureDocumentOutline(path);
    m_tocPanel->setOutline(outline);
}

void MainWindow::navigateDocumentPage(int page_1based)
{
    if (page_1based < 1 || !m_imageView) {
        return;
    }
    QString path = m_imageView->classicPath();
    if (path.isEmpty() && m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
        path = m_session.paths().at(m_currentIndex);
    }
    if (path.isEmpty()) {
        return;
    }
    // Prefer jumping within the session list.
    const QString doc = PagePath::documentFilePath(path);
    const QString layout = PagePath::epubLayoutParamsOf(path);
    for (int i = 0; i < m_session.paths().size(); ++i) {
        const QString &p = m_session.paths().at(i);
        if (!PagePath::isPageRef(p)) {
            continue;
        }
        if (PagePath::documentFilePath(p) == doc && PagePath::pageNumber(p) == page_1based) {
            // Navigate session index — reuse existing go-to if any.
            if (i != m_currentIndex) {
                m_currentIndex = i;
                m_imageView->loadImage(p);
                updateStatus();
                updateTocPanel();
            }
            return;
        }
    }
    // Build a page path even if not in session.
    QString target;
    if (!layout.isEmpty() || path.contains(QLatin1String("//epub:"))) {
        target = PagePath::makeEpubRef(doc, page_1based,
                                       layout.isEmpty() ? PagePath::epubLayoutParamsOf(path)
                                                        : layout);
    } else {
        target = PagePath::makeRef(doc, page_1based);
    }
    if (!target.isEmpty()) {
        m_imageView->loadImage(target);
        updateStatus();
    }
}

void MainWindow::openDocumentLinkUri(const QString &uri)
{
    if (uri.isEmpty()) {
        return;
    }
    // Internal-looking URIs may still be page jumps.
    if (uri.startsWith(QLatin1Char('#'))) {
        int page = 0;
        if (uri.mid(1).startsWith(QLatin1String("page="))) {
            page = uri.mid(6).toInt();
        } else {
            page = uri.mid(1).toInt();
        }
        if (page > 0) {
            navigateDocumentPage(page);
            return;
        }
    }
    const QUrl url(uri);
    // Spine-relative EPUB paths (…xhtml / …html / …xml) are not web URLs —
    // thumtoo should have resolved them to page numbers. Do not hand them to
    // the desktop browser (opens nothing useful).
    const QString lower = uri.toLower();
    const bool looksInternalPath =
        url.scheme().isEmpty()
        || url.isRelative()
        || lower.endsWith(QLatin1String(".xhtml"))
        || lower.endsWith(QLatin1String(".html"))
        || lower.endsWith(QLatin1String(".htm"))
        || lower.endsWith(QLatin1String(".xml"))
        || lower.contains(QLatin1String(".xhtml#"))
        || lower.contains(QLatin1String(".html#"));
    if (looksInternalPath) {
        if (statusBar()) {
            statusBar()->showMessage(
                tr("Unresolved document link: %1").arg(uri), 5000);
        }
        return;
    }
    if (url.isValid() && !url.scheme().isEmpty()) {
        QDesktopServices::openUrl(url);
    }
}

void MainWindow::updateMetadataPanel()
{
    if (!m_metadataPanel || !m_imageView) {
        return;
    }
    // Hidden dock: do not decode on every Gallery/Workspace selection.
    // VisibilityChanged reconnect refreshes when the user opens the panel.
    if (m_metadataDock && !m_metadataDock->isVisible()) {
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
    if (path == m_metadataPath && !path.isEmpty()) {
        return;
    }
    m_metadataPath = path;
    if (path.isEmpty()) {
        m_metadataPanel->clear();
        return;
    }
    // Reuse already-decoded Gallery/Workspace tile pixels when present.
    QImage hint;
    if (ImageItem *item = m_imageView->findPreferredItemForPath(path)) {
        if (item->hasDecodedPixels()) {
            hint = item->sourceImage();
        }
    }
    m_metadataPanel->setImagePath(path, hint);
}

void MainWindow::updateAdjustmentsPanel()
{
    if (!m_adjustmentsPanel || !m_imageView) {
        return;
    }
    // Hidden dock: never copy tile pixels on Gallery selection (was a major lag spike).
    if (m_adjustmentsDock && !m_adjustmentsDock->isVisible()) {
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
    // Preview is optional chrome — keep it cheap (scaled down).
    QPixmap pm = item->pixmap();
    if (!pm.isNull() && (pm.width() > 512 || pm.height() > 512)) {
        pm = pm.scaled(512, 512, Qt::KeepAspectRatio, Qt::FastTransformation);
    }
    m_adjustmentsPanel->setPreviewImage(pm.toImage());
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
    if (m_tocDock && m_tocDock->isVisible()) {
        updateTocPanel();
    }
    if (m_imageView && !m_imageView->linkHoverTip().isEmpty()) {
        statusBar()->showMessage(m_imageView->linkHoverTip());
    }

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
                tr("Could not load “%1”").arg(PagePath::displayName(err)), 5000);
        }
        // Drag/open decode progress (Gallery virtualization + filmstrip ladder).
        // Use m_decodeStatusActive — do not match message text (breaks under i18n).
        int pending = m_imageView->pendingDecodeCount();
        if (m_thumbnailBar) {
            pending += m_thumbnailBar->pendingLoadCount();
        }
        if (pending > 0 && statusBar()) {
            statusBar()->showMessage(
                tr("Loading %n thumbnail…", "thumb/decode progress", pending), 0);
            m_decodeStatusActive = true;
        } else if (m_decodeStatusActive && statusBar()) {
            statusBar()->clearMessage();
            m_decodeStatusActive = false;
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
    if (m_resetContentAppearanceAct) {
        menu.addAction(m_resetContentAppearanceAct);
    }
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
    // Menu bar is hidden in fullscreen; surface slideshow controls here.
    if (isFullScreen() && m_slideshowAct) {
        menu.addAction(m_slideshowAct);
    }
    if (m_slideshowSettingsAct) {
        menu.addAction(m_slideshowSettingsAct);
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

    // Leaving fullscreen ends the slideshow session (playing or paused).
    if (!fs && isSlideshowSession()) {
        stopSlideshow();
    }

    if (fs) {
        m_toolBarVisibleBeforeFullscreen = m_toolBar->isVisible();
        m_thumbnailBarVisibleBeforeFullscreen =
            m_thumbnailDock ? m_thumbnailDock->isVisible() : m_thumbnailBar->isVisible();
        m_metadataVisibleBeforeFullscreen =
            m_metadataDock && m_metadataDock->isVisible();
        m_layoutVisibleBeforeFullscreen =
            m_layoutDock && m_layoutDock->isVisible();
        m_adjustmentsVisibleBeforeFullscreen =
            m_adjustmentsDock && m_adjustmentsDock->isVisible();
        m_toolBar->setVisible(false);
        if (m_workspaceToolBar) {
            m_workspaceToolBar->setVisible(false);
        }
        if (m_thumbnailDock) {
            m_thumbnailDock->setVisible(false);
        } else {
            m_thumbnailBar->setVisible(false);
        }
        m_metadataDock->setVisible(false);
        if (m_layoutDock) {
            m_layoutDock->setVisible(false);
        }
        if (m_adjustmentsDock) {
            m_adjustmentsDock->setVisible(false);
        }
        m_toggleToolBarAct->setChecked(false);
        m_toggleThumbnailBarAct->setChecked(false);
        m_toggleMetadataAct->setChecked(false);
        if (m_toggleLayoutPanelAct) {
            m_toggleLayoutPanelAct->setChecked(false);
        }
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(false);
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
        if (m_adjustmentsDock) {
            m_adjustmentsDock->setVisible(m_adjustmentsVisibleBeforeFullscreen);
        }
        if (m_toggleAdjustmentsAct) {
            m_toggleAdjustmentsAct->setChecked(m_adjustmentsVisibleBeforeFullscreen);
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
    // Fullscreen is not a persistent preference — only --fullscreen / -f starts
    // that way. QWidget::saveGeometry() encodes WindowFullScreen and would
    // reopen full-screen after a prior session left that way.
    if (isFullScreen()) {
        setWindowState(windowState() & ~Qt::WindowFullScreen);
        showNormal();
        const QRect normal = settings.value(QStringLiteral("normalGeometry")).toRect();
        if (normal.isValid()) {
            setGeometry(normal);
        }
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
    {
        const bool pinned = settings.value(QStringLiteral("locationBarPinned"), false).toBool();
        const bool searchPinned = settings.value(QStringLiteral("searchBarPinned"), false).toBool();
        setLocationBarPinned(pinned);
        setSearchBarPinned(searchPinned);
    }

    // restoreState can put the location bar back on the same row as the main
    // toolbar (from sessions saved before the dedicated-row layout). Force a
    // break so it always sits on its own row under the main toolbar.
    // Re-apply visibility afterwards — insertToolBarBreak / restoreState can
    // leave the bar shown even when it is not pinned.
    if (m_locationBar) {
        insertToolBarBreak(m_locationBar);
        m_locationBar->setVisible(m_locationBarPinned);
    }
    if (m_searchBar) {
        insertToolBarBreak(m_searchBar);
        m_searchBar->setVisible(m_searchBarPinned);
    }

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
    if (m_imageView) {
        const int transitionKind = settings.value(QStringLiteral("slideshowTransition"), 1).toInt();
        m_imageView->setSlideshowTransition(
            static_cast<ImageView::SlideshowTransition>(qBound(0, transitionKind, 3)));
        {
            // Full transition (out + in); cap at the interval.
            const int intervalCap = m_slideshowIntervalMs;
            const int transitionMs =
                settings.value(QStringLiteral("slideshowTransitionDurationMs"), 400).toInt();
            m_imageView->setSlideshowTransitionDurationMs(
                qMin(transitionMs, qMax(0, intervalCap)));
        }
        m_imageView->setSlideshowMotion(
            static_cast<ImageView::SlideshowMotion>(
                qBound(0, settings.value(QStringLiteral("slideshowMotion"), 0).toInt(), 2)));
        m_imageView->setPanZoomFactor(
            settings.value(QStringLiteral("slideshowPanZoomFactor"), 1.12).toDouble());
        m_imageView->setSlideshowZoom(
            static_cast<ImageView::SlideshowZoom>(
                qBound(0, settings.value(QStringLiteral("slideshowZoomMode"), 0).toInt(), 2)));
        const QColor pad = QColor(settings.value(QStringLiteral("slideshowPadColor"),
            m_imageView->backgroundColor().name(QColor::HexRgb)).toString());
        if (pad.isValid()) {
            m_imageView->setSlideshowPadColor(pad);
        }
        m_imageView->setSlideshowLetterboxFill(
            static_cast<ImageView::SlideshowLetterboxFill>(
                qBound(0, settings.value(QStringLiteral("slideshowLetterboxFill"), 0).toInt(), 2)));
    }
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
            && layoutInt <= int(ImageView::LayoutMode::Facing)) {
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
        const bool cropThumbs = settings.value(QStringLiteral("thumbnailCropToSquare"), false).toBool();
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
    // Filmstrip lives in ThumbnailDock; geometry is part of windowState.
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
            if (m_thumbnailBar) {
                m_thumbnailBar->setStripBackground(bg);
            }
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
    // Persist the normal frame geometry, not a fullscreen state bit.
    if (isFullScreen()) {
        settings.setValue(QStringLiteral("geometry"),
                          saveGeometry()); // still has FS bit — stripped on read
        // Also store normalGeometry for a cleaner reopen size.
        settings.setValue(QStringLiteral("normalGeometry"), normalGeometry());
    } else {
        settings.setValue(QStringLiteral("geometry"), saveGeometry());
        settings.setValue(QStringLiteral("normalGeometry"), geometry());
    }
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
    settings.setValue(QStringLiteral("locationBarPinned"), m_locationBarPinned);
    settings.setValue(QStringLiteral("searchBarPinned"), m_searchBarPinned);
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
    if (m_imageView) {
        settings.setValue(QStringLiteral("slideshowTransition"),
                          static_cast<int>(m_imageView->slideshowTransition()));
        settings.setValue(QStringLiteral("slideshowTransitionDurationMs"),
                          m_imageView->slideshowTransitionDurationMs());
        settings.setValue(QStringLiteral("slideshowMotion"),
                          static_cast<int>(m_imageView->slideshowMotion()));
        settings.setValue(QStringLiteral("slideshowPanZoomFactor"),
                          m_imageView->panZoomFactor());
        settings.setValue(QStringLiteral("slideshowZoomMode"),
                          static_cast<int>(m_imageView->slideshowZoom()));
        settings.setValue(QStringLiteral("slideshowLetterboxFill"),
                          static_cast<int>(m_imageView->slideshowLetterboxFill()));
        settings.setValue(QStringLiteral("slideshowPadColor"),
                          m_imageView->slideshowPadColor().name(QColor::HexRgb));
    }
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
    settings.remove(QStringLiteral("centralSplitter"));
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
        if (m_locationEdit && m_locationEdit->hasFocus()) {
            cancelLocationBar();
            event->accept();
            return;
        }
        if (m_searchEdit && m_searchEdit->hasFocus()) {
            cancelSearchBar();
            event->accept();
            return;
        }
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
        if (m_imageView && m_imageView->isImageMode() && !m_session.paths().isEmpty()) {
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
    if (!event->mimeData()) {
        return;
    }
    if (event->mimeData()->hasFormat(QStringLiteral("application/x-biltoo-paths"))
        || !extractLocalImagePaths(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void MainWindow::handleDroppedUrls(const QList<QUrl> &urls, Qt::KeyboardModifiers modifiers,
                                   const QPointF &scenePos, bool hasScenePos,
                                   const QList<qint64> &sessionIds,
                                   const QStringList &internalPaths)
{
    // Prefer explicit internal paths from the filmstrip (exact selection,
    // including //archive: members). URL-only drops still go through
    // extractLocalImagePaths + expandPaths (directories / archive files).
    QStringList paths = internalPaths;
    paths.removeAll(QString());
    bool fromInternalSelection = !paths.isEmpty();
    if (!fromInternalSelection) {
        QMimeData mime;
        mime.setUrls(urls);
        paths = extractLocalImagePaths(&mime);
    }
    if (paths.isEmpty()) {
        return;
    }

    // Workspace mode: place each drop as a canvas instance at the drop point.
    // Paths already on the canvas are *duplicated* (not moved); a new session
    // slot is appended so the filmstrip can address the copy independently.
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr,
                "biltoo/drop: handle mode=W%d G%d I%d hasPos=%d scene=(%.1f,%.1f) "
                "paths=%lld sessionIds=%lld internal=%lld\n",
                isWorkspaceMode() ? 1 : 0, isGalleryMode() ? 1 : 0,
                (m_imageView && m_imageView->isImageMode()) ? 1 : 0,
                hasScenePos ? 1 : 0, scenePos.x(), scenePos.y(),
                static_cast<long long>(paths.size()),
                static_cast<long long>(sessionIds.size()),
                static_cast<long long>(internalPaths.size()));
    }
    if (isWorkspaceMode()) {
        const QStringList expanded = fromInternalSelection ? paths : expandPaths(paths);
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
            if (hasScenePos) {
                const QPointF pos = scenePos + QPointF(28.0 * i, 22.0 * i);
                if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
                    dbg && dbg[0] != '\0' && dbg[0] != '0') {
                    fprintf(stderr,
                            "biltoo/drop: placeOrMove path=%s sid=%lld slot=%d "
                            "pos=(%.1f,%.1f) alreadyOnCanvas=%d\n",
                            qPrintable(img), static_cast<long long>(sid), slot,
                            pos.x(), pos.y(), alreadyOnCanvas ? 1 : 0);
                }
                // placeOrMoveImageAt owns identity via PendingSessionBind / move-by-id.
                // Do NOT bindSelectedSessionIds here — that stamped sid onto every
                // currently selected tile and created duplicate SessionImageIds.
                m_imageView->placeOrMoveImageAt(img, pos, sid, slot);
            } else {
                if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
                    dbg && dbg[0] != '\0' && dbg[0] != '0') {
                    fprintf(stderr,
                            "biltoo/drop: NO scene pos — addImageForSession path=%s "
                            "sid=%lld (appearance may restore old pose)\n",
                            qPrintable(img), static_cast<long long>(sid));
                }
                m_imageView->addImageForSession(img, sid, slot);
            }
            ++i;
        }
        // Defer membership sync — rebind during the drop stack asserted under
        // Qt 6.11 ("destructor may have already run" / wrong type).
        QTimer::singleShot(0, this, [this]() {
            if (!isWorkspaceMode()) {
                return;
            }
            syncThumbnailCanvasMembership();
            if (m_session.paths().size() > 1 && m_thumbnailBar
                && !(m_thumbnailDock ? m_thumbnailDock->isVisible()
                                    : m_thumbnailBar->isVisible())) {
                m_toggleThumbnailBarAct->setChecked(true);
                if (m_thumbnailDock) {
                    m_thumbnailDock->setVisible(true);
                } else {
                    m_thumbnailBar->setVisible(true);
                }
            }
            updateStatus();
        });
        return;
    }

    // Gallery mode: each dropped path becomes a new session row (allows the
    // same file more than once). Thumbnail-bar drags therefore duplicate;
    // external files are appended as usual.
    if (isGalleryMode()) {
        const QStringList expanded = fromInternalSelection ? paths : expandPaths(paths);
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
    const QStringList expanded = fromInternalSelection ? paths : expandPaths(paths);
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
                                const QPointF &scenePos, bool hasScenePos,
                                const QList<qint64> &sessionIds,
                                const QStringList &internalPaths)
{
    handleDroppedUrls(urls, modifiers, scenePos, hasScenePos, sessionIds, internalPaths);
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()) {
        return;
    }
    const QByteArray pathBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-paths"));
    const bool hasInternal = !pathBytes.isEmpty();
    if (!event->mimeData()->hasUrls() && !hasInternal) {
        return;
    }
    QList<qint64> sessionIds;
    const QByteArray idBytes =
        event->mimeData()->data(QStringLiteral("application/x-biltoo-session-ids"));
    if (!idBytes.isEmpty()) {
        for (const QByteArray &tok : idBytes.split(',')) {
            bool ok = false;
            const qint64 v = tok.trimmed().toLongLong(&ok);
            sessionIds.append(ok ? v : 0);
        }
    }
    QStringList internalPaths;
    if (hasInternal) {
        internalPaths = QString::fromUtf8(pathBytes).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    // Prefer mapping through the canvas when the cursor is over ImageView —
    // filmstrip→Workspace drops often land on MainWindow if the OpenGL
    // viewport does not deliver Drop to ImageView::dropEvent.
    QPointF scenePos;
    bool hasScenePos = false;
    if (m_imageView && m_imageView->viewport()) {
        const QPoint viewPos = m_imageView->viewport()->mapFromGlobal(QCursor::pos());
        if (m_imageView->viewport()->rect().contains(viewPos)) {
            scenePos = m_imageView->mapToScene(viewPos);
            hasScenePos = true;
        }
    }
    if (const char *dbg = std::getenv("BILTOO_DEBUG_DROP");
        dbg && dbg[0] != '\0' && dbg[0] != '0') {
        fprintf(stderr, "biltoo/drop: MainWindow::dropEvent hasPos=%d scene=(%.1f,%.1f)\n",
                hasScenePos ? 1 : 0, scenePos.x(), scenePos.y());
    }
    handleDroppedUrls(event->mimeData()->urls(), event->modifiers(), scenePos,
                      hasScenePos, sessionIds, internalPaths);
    event->acceptProposedAction();
}
