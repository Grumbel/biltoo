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

namespace {

QString imageFileDialogFilter()
{
    QStringList patterns;
    for (const QString &suffix : ImageLoader::imageSuffixes()) {
        patterns.append(QStringLiteral("*.%1").arg(suffix));
    }
    return QObject::tr("Images (%1);;All Files (*)").arg(patterns.join(QLatin1Char(' ')));
}

} // namespace

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

void MainWindow::createActions()
{
    m_openAct = new QAction(tr("&Open..."), this);
    m_openAct->setShortcut(QKeySequence::Open);
    m_openAct->setIcon(themeIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton));
    m_openAct->setStatusTip(tr("Open image files (replace session)"));
    connect(m_openAct, &QAction::triggered, this, &MainWindow::openFiles);

    m_addAct = new QAction(tr("&Add Images..."), this);
    m_addAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_A);
    m_addAct->setIcon(themeIcon(QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder));
    m_addAct->setStatusTip(tr("Add image files to the current session (Ctrl+Shift+A)"));
    connect(m_addAct, &QAction::triggered, this, &MainWindow::addFiles);

    m_openDirAct = new QAction(tr("Open &Directory..."), this);
    m_openDirAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_O);
    m_openDirAct->setIcon(themeIcon(QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon));
    m_openDirAct->setStatusTip(tr("Open all images in a directory"));
    connect(m_openDirAct, &QAction::triggered, this, &MainWindow::openDirectory);

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
    // F is reserved for fullscreen (common image-viewer convention)
    m_zoomFitAct->setIcon(themeIcon(QStringLiteral("zoom-fit-best"), QStyle::SP_TitleBarMaxButton));
    m_zoomFitAct->setStatusTip(tr("Fit image to the window"));
    connect(m_zoomFitAct, &QAction::triggered, this, &MainWindow::zoomFit);

    m_zoomFillAct = new QAction(tr("Zoom to F&ill"), this);
    m_zoomFillAct->setShortcut(Qt::CTRL | Qt::Key_F);
    m_zoomFillAct->setIcon(themeIcon(QStringLiteral("zoom-fit-best"), QStyle::SP_TitleBarMaxButton));
    m_zoomFillAct->setStatusTip(tr("Fill the window (may crop the image)"));
    connect(m_zoomFillAct, &QAction::triggered, this, &MainWindow::zoomFill);

    m_fullscreenAct = new QAction(tr("F&ullscreen"), this);
    m_fullscreenAct->setShortcuts({Qt::Key_F, Qt::Key_F11});
    m_fullscreenAct->setIcon(themeIcon(QStringLiteral("view-fullscreen"), QStyle::SP_TitleBarMaxButton));
    m_fullscreenAct->setCheckable(true);
    m_fullscreenAct->setStatusTip(tr("Toggle fullscreen mode (F or F11)"));
    connect(m_fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    m_rotateLeftAct = new QAction(tr("Rotate &Left"), this);
    m_rotateLeftAct->setShortcuts({Qt::CTRL | Qt::Key_L, Qt::Key_BracketLeft});
    m_rotateLeftAct->setIcon(themeIcon(QStringLiteral("object-rotate-left"), QStyle::SP_ArrowBack));
    m_rotateLeftAct->setStatusTip(tr("Rotate 90° counter-clockwise"));
    connect(m_rotateLeftAct, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_rotateRightAct = new QAction(tr("Rotate &Right"), this);
    m_rotateRightAct->setShortcuts({Qt::CTRL | Qt::Key_R, Qt::Key_BracketRight, Qt::Key_R});
    m_rotateRightAct->setIcon(themeIcon(QStringLiteral("object-rotate-right"), QStyle::SP_ArrowForward));
    m_rotateRightAct->setStatusTip(tr("Rotate 90° clockwise"));
    connect(m_rotateRightAct, &QAction::triggered, this, &MainWindow::rotateRight);

    m_flipHAct = new QAction(tr("Flip &Horizontal"), this);
    m_flipHAct->setShortcut(Qt::CTRL | Qt::Key_H);
    m_flipHAct->setIcon(themeIcon(QStringLiteral("object-flip-horizontal"), QStyle::SP_BrowserReload));
    m_flipHAct->setStatusTip(tr("Flip image horizontally"));
    connect(m_flipHAct, &QAction::triggered, this, &MainWindow::flipHorizontal);

    m_flipVAct = new QAction(tr("Flip &Vertical"), this);
    m_flipVAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_H);
    m_flipVAct->setIcon(themeIcon(QStringLiteral("object-flip-vertical"), QStyle::SP_BrowserReload));
    m_flipVAct->setStatusTip(tr("Flip image vertically"));
    connect(m_flipVAct, &QAction::triggered, this, &MainWindow::flipVertical);

    m_toggleHudAct = new QAction(tr("Show &HUD Overlay"), this);
    m_toggleHudAct->setShortcut(Qt::Key_H);
    m_toggleHudAct->setCheckable(true);
    m_toggleHudAct->setStatusTip(tr("Show an on-image overlay with filename, zoom and size"));
    connect(m_toggleHudAct, &QAction::triggered, this, &MainWindow::toggleHud);

    m_hideThumbLabelsAct = new QAction(tr("Hide Thumbnail &Filenames"), this);
    m_hideThumbLabelsAct->setCheckable(true);
    m_hideThumbLabelsAct->setStatusTip(tr("Hide filenames under thumbnails"));
    connect(m_hideThumbLabelsAct, &QAction::triggered, this, &MainWindow::toggleThumbnailLabels);

    m_previousAct = new QAction(tr("&Previous Image"), this);
    m_previousAct->setShortcuts({Qt::Key_Left, Qt::Key_Backspace, Qt::Key_PageUp});
    m_previousAct->setIcon(themeIcon(QStringLiteral("go-previous"), QStyle::SP_ArrowBack));
    m_previousAct->setStatusTip(tr("Show previous image"));
    connect(m_previousAct, &QAction::triggered, this, &MainWindow::goPrevious);

    m_nextAct = new QAction(tr("&Next Image"), this);
    m_nextAct->setShortcuts({Qt::Key_Right, Qt::Key_PageDown});
    m_nextAct->setIcon(themeIcon(QStringLiteral("go-next"), QStyle::SP_ArrowForward));
    m_nextAct->setStatusTip(tr("Show next image"));
    connect(m_nextAct, &QAction::triggered, this, &MainWindow::goNext);

    m_firstAct = new QAction(tr("&First Image"), this);
    m_firstAct->setShortcut(Qt::Key_Home);
    m_firstAct->setIcon(themeIcon(QStringLiteral("go-first"), QStyle::SP_MediaSkipBackward));
    m_firstAct->setStatusTip(tr("Show the first image in the session"));
    connect(m_firstAct, &QAction::triggered, this, &MainWindow::goFirst);

    m_lastAct = new QAction(tr("&Last Image"), this);
    m_lastAct->setShortcut(Qt::Key_End);
    m_lastAct->setIcon(themeIcon(QStringLiteral("go-last"), QStyle::SP_MediaSkipForward));
    m_lastAct->setStatusTip(tr("Show the last image in the session"));
    connect(m_lastAct, &QAction::triggered, this, &MainWindow::goLast);

    m_slideshowAct = new QAction(tr("Play &Slideshow"), this);
    m_slideshowAct->setShortcut(Qt::Key_Space);
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    m_slideshowAct->setCheckable(true);
    m_slideshowAct->setStatusTip(
        tr("Start or stop the slideshow (Space). Unavailable in workspace mode."));
    connect(m_slideshowAct, &QAction::triggered, this, &MainWindow::toggleSlideshow);

    m_workspaceModeAct = new QAction(tr("&Workspace Mode"), this);
    m_workspaceModeAct->setCheckable(true);
    m_workspaceModeAct->setChecked(false);
    m_workspaceModeAct->setIcon(themeIcon(QStringLiteral("view-paged"), QStyle::SP_DesktopIcon));
    m_workspaceModeAct->setStatusTip(
        tr("Compare multiple images on the view. Click thumbnails to show or hide them; "
           "each image keeps its position and size."));
    connect(m_workspaceModeAct, &QAction::triggered, this, &MainWindow::toggleWorkspaceMode);

    m_selectToolAct = new QAction(tr("&Select"), this);
    m_selectToolAct->setCheckable(true);
    m_selectToolAct->setChecked(true);
    m_selectToolAct->setIcon(themeIcon(QStringLiteral("edit-select"), QStyle::SP_FileDialogContentsView));
    m_selectToolAct->setStatusTip(tr("Select and move images on the workspace"));
    connect(m_selectToolAct, &QAction::triggered, this, &MainWindow::setSelectTool);

    m_panToolAct = new QAction(tr("&Pan"), this);
    m_panToolAct->setCheckable(true);
    m_panToolAct->setIcon(themeIcon(QStringLiteral("transform-move"), QStyle::SP_ArrowRight));
    m_panToolAct->setStatusTip(tr("Pan the workspace view"));
    connect(m_panToolAct, &QAction::triggered, this, &MainWindow::setPanTool);

    auto *toolGroup = new QActionGroup(this);
    toolGroup->addAction(m_selectToolAct);
    toolGroup->addAction(m_panToolAct);
    toolGroup->setExclusive(true);

    m_undoAct = m_imageView->undoStack()->createUndoAction(this, tr("&Undo"));
    m_undoAct->setShortcuts(QKeySequence::Undo);
    m_undoAct->setIcon(themeIcon(QStringLiteral("edit-undo"), QStyle::SP_ArrowBack));
    m_redoAct = m_imageView->undoStack()->createRedoAction(this, tr("&Redo"));
    m_redoAct->setShortcuts(QKeySequence::Redo);
    m_redoAct->setIcon(themeIcon(QStringLiteral("edit-redo"), QStyle::SP_ArrowForward));

    m_selectAllAct = new QAction(tr("Select &All"), this);
    m_selectAllAct->setShortcut(QKeySequence::SelectAll);
    m_selectAllAct->setIcon(themeIcon(QStringLiteral("edit-select-all"), QStyle::SP_DialogApplyButton));
    m_selectAllAct->setStatusTip(
        tr("Select all thumbnails (in workspace mode: show all on the canvas)"));
    connect(m_selectAllAct, &QAction::triggered, this, &MainWindow::selectAllThumbnails);

    m_layoutFreeFormAct = new QAction(tr("&Free Form Layout"), this);
    m_layoutFreeFormAct->setCheckable(true);
    m_layoutFreeFormAct->setChecked(true);
    m_layoutFreeFormAct->setIcon(themeIcon(QStringLiteral("transform-move"), QStyle::SP_FileDialogDetailedView));
    m_layoutFreeFormAct->setStatusTip(tr("Place and move images freely on the workspace"));
    connect(m_layoutFreeFormAct, &QAction::triggered, this, &MainWindow::setLayoutFreeForm);

    m_layoutSideBySideAct = new QAction(tr("Layout &Horizontal"), this);
    m_layoutSideBySideAct->setCheckable(true);
    m_layoutSideBySideAct->setShortcut(Qt::CTRL | Qt::Key_Y);
    m_layoutSideBySideAct->setIcon(resourceIcon(QStringLiteral("gallery-side-by-side")));
    m_layoutSideBySideAct->setStatusTip(
        tr("Gallery: horizontal strip (fit height, scroll sideways)"));
    connect(m_layoutSideBySideAct, &QAction::triggered, this, &MainWindow::setLayoutSideBySide);

    m_layoutVerticalAct = new QAction(tr("Layout &Vertical"), this);
    m_layoutVerticalAct->setCheckable(true);
    m_layoutVerticalAct->setIcon(resourceIcon(QStringLiteral("gallery-vertical")));
    m_layoutVerticalAct->setStatusTip(tr("Gallery: arrange images in a vertical column"));
    connect(m_layoutVerticalAct, &QAction::triggered, this, &MainWindow::setLayoutVertical);

    m_layoutGridAct = new QAction(tr("Layout &Grid"), this);
    m_layoutGridAct->setCheckable(true);
    m_layoutGridAct->setIcon(resourceIcon(QStringLiteral("gallery-grid")));
    m_layoutGridAct->setStatusTip(tr("Gallery: grid with whole images (letterboxed in cells)"));
    connect(m_layoutGridAct, &QAction::triggered, this, &MainWindow::setLayoutGrid);

    m_layoutGridCropAct = new QAction(tr("Layout Grid &Crop"), this);
    m_layoutGridCropAct->setCheckable(true);
    m_layoutGridCropAct->setIcon(resourceIcon(QStringLiteral("gallery-grid-crop")));
    m_layoutGridCropAct->setStatusTip(
        tr("Gallery: square grid, images centre-cropped to fill each cell"));
    connect(m_layoutGridCropAct, &QAction::triggered, this, &MainWindow::setLayoutGridCrop);

    m_layoutStackAct = new QAction(tr("Layout Stac&k"), this);
    m_layoutStackAct->setCheckable(true);
    m_layoutStackAct->setIcon(resourceIcon(QStringLiteral("gallery-stack")));
    m_layoutStackAct->setStatusTip(tr("Stack images on top of each other for opacity comparison"));
    connect(m_layoutStackAct, &QAction::triggered, this, &MainWindow::setLayoutStack);

    m_layoutMasonryAct = new QAction(tr("Layout &Masonry"), this);
    m_layoutMasonryAct->setCheckable(true);
    m_layoutMasonryAct->setIcon(resourceIcon(QStringLiteral("gallery-masonry")));
    m_layoutMasonryAct->setStatusTip(
        tr("Gallery: pack into N columns that fill the window width"));
    connect(m_layoutMasonryAct, &QAction::triggered, this, &MainWindow::setLayoutMasonry);

    m_layoutMasonryRowsAct = new QAction(tr("Layout Masonry &Rows"), this);
    m_layoutMasonryRowsAct->setCheckable(true);
    m_layoutMasonryRowsAct->setIcon(resourceIcon(QStringLiteral("gallery-masonry-rows")));
    m_layoutMasonryRowsAct->setStatusTip(
        tr("Gallery: pack into N rows that fill the window height"));
    connect(m_layoutMasonryRowsAct, &QAction::triggered, this, &MainWindow::setLayoutMasonryRows);

    m_backToGalleryAct = new QAction(tr("&Up"), this);
    m_backToGalleryAct->setIcon(themeIcon(QStringLiteral("go-up"), QStyle::SP_ArrowUp));
    m_backToGalleryAct->setStatusTip(tr("Up to gallery"));
    m_backToGalleryAct->setToolTip(tr("Up to gallery"));
    m_backToGalleryAct->setEnabled(false);
    connect(m_backToGalleryAct, &QAction::triggered, this, &MainWindow::returnToGallery);

    // Gallery layouts only (Workspace Mode is separate; Free Form is not a layout).
    auto *layoutGroup = new QActionGroup(this);
    layoutGroup->addAction(m_layoutSideBySideAct);
    layoutGroup->addAction(m_layoutVerticalAct);
    layoutGroup->addAction(m_layoutGridAct);
    layoutGroup->addAction(m_layoutGridCropAct);
    layoutGroup->addAction(m_layoutMasonryAct);
    layoutGroup->addAction(m_layoutMasonryRowsAct);
    layoutGroup->addAction(m_layoutStackAct);
    layoutGroup->setExclusive(true);
    // No default checked gallery layout until the user chooses one.
    for (QAction *act : layoutGroup->actions()) {
        act->setChecked(false);
    }

    m_raiseAct = new QAction(tr("&Raise"), this);
    m_raiseAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Up);
    m_raiseAct->setIcon(themeIcon(QStringLiteral("go-up"), QStyle::SP_ArrowUp));
    m_raiseAct->setStatusTip(tr("Raise selected workspace image"));
    connect(m_raiseAct, &QAction::triggered, this, &MainWindow::raiseSelected);

    m_lowerAct = new QAction(tr("&Lower"), this);
    m_lowerAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Down);
    m_lowerAct->setIcon(themeIcon(QStringLiteral("go-down"), QStyle::SP_ArrowDown));
    m_lowerAct->setStatusTip(tr("Lower selected workspace image"));
    connect(m_lowerAct, &QAction::triggered, this, &MainWindow::lowerSelected);

    m_opacityUpAct = new QAction(tr("Opacity &Up"), this);
    m_opacityUpAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    m_opacityUpAct->setStatusTip(tr("Increase opacity of the selected image"));
    connect(m_opacityUpAct, &QAction::triggered, this, &MainWindow::opacityUp);

    m_opacityDownAct = new QAction(tr("Opacity &Down"), this);
    m_opacityDownAct->setShortcut(Qt::CTRL | Qt::Key_Minus);
    m_opacityDownAct->setStatusTip(tr("Decrease opacity of the selected image"));
    connect(m_opacityDownAct, &QAction::triggered, this, &MainWindow::opacityDown);

    m_opacityResetAct = new QAction(tr("Opacity &Reset"), this);
    m_opacityResetAct->setStatusTip(tr("Reset opacity of the selected image to 100%"));
    connect(m_opacityResetAct, &QAction::triggered, this, &MainWindow::opacityReset);

    m_clearExtrasAct = new QAction(tr("Clear Workspace &Extras"), this);
    m_clearExtrasAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_W);
    m_clearExtrasAct->setIcon(themeIcon(QStringLiteral("edit-clear"), QStyle::SP_DialogResetButton));
    m_clearExtrasAct->setStatusTip(tr("Remove comparison images; keep the primary image"));
    connect(m_clearExtrasAct, &QAction::triggered, this, &MainWindow::clearWorkspaceExtras);

    m_sortNameAct = new QAction(tr("Sort by &Name"), this);
    m_sortNameAct->setCheckable(true);
    m_sortNameAct->setChecked(true);
    m_sortNameAct->setIcon(themeIcon(QStringLiteral("view-sort-ascending"), QStyle::SP_ArrowDown));
    m_sortNameAct->setStatusTip(tr("Sort images by file name"));
    connect(m_sortNameAct, &QAction::triggered, this, &MainWindow::sortByName);

    m_sortMTimeAct = new QAction(tr("Sort by &Date"), this);
    m_sortMTimeAct->setCheckable(true);
    m_sortMTimeAct->setIcon(themeIcon(QStringLiteral("view-calendar"), QStyle::SP_FileDialogDetailedView));
    m_sortMTimeAct->setStatusTip(tr("Sort images by modification time"));
    connect(m_sortMTimeAct, &QAction::triggered, this, &MainWindow::sortByMTime);

    m_sortGroup = new QActionGroup(this);
    m_sortGroup->addAction(m_sortNameAct);
    m_sortGroup->addAction(m_sortMTimeAct);
    m_sortGroup->setExclusive(true);

    m_toggleToolBarAct = new QAction(tr("Show &Toolbar"), this);
    m_toggleToolBarAct->setShortcut(Qt::CTRL | Qt::Key_T);
    m_toggleToolBarAct->setShortcutContext(Qt::ApplicationShortcut);
    m_toggleToolBarAct->setCheckable(true);
    m_toggleToolBarAct->setChecked(true);
    m_toggleToolBarAct->setIcon(themeIcon(QStringLiteral("configure-toolbars"), QStyle::SP_ToolBarHorizontalExtensionButton));
    m_toggleToolBarAct->setStatusTip(tr("Show or hide the toolbar (Ctrl+T)"));
    connect(m_toggleToolBarAct, &QAction::triggered, this, &MainWindow::toggleToolBar);

    m_toggleThumbnailBarAct = new QAction(tr("Show Thum&bnails"), this);
    m_toggleThumbnailBarAct->setShortcut(Qt::CTRL | Qt::Key_M);
    m_toggleThumbnailBarAct->setCheckable(true);
    m_toggleThumbnailBarAct->setChecked(false);
    m_toggleThumbnailBarAct->setIcon(themeIcon(QStringLiteral("view-list-icons"), QStyle::SP_FileDialogListView));
    m_toggleThumbnailBarAct->setStatusTip(tr("Show or hide the thumbnail bar"));
    connect(m_toggleThumbnailBarAct, &QAction::triggered, this, &MainWindow::toggleThumbnailBar);

    // Use the dock's own toggle action so the close button and menu/toolbar stay in sync
    m_toggleMetadataAct = m_metadataDock->toggleViewAction();
    m_toggleMetadataAct->setText(tr("Show &Metadata"));
    m_toggleMetadataAct->setShortcut(Qt::CTRL | Qt::Key_E);
    m_toggleMetadataAct->setIcon(themeIcon(QStringLiteral("dialog-information"), QStyle::SP_FileDialogInfoView));
    m_toggleMetadataAct->setStatusTip(tr("Show or hide the metadata side panel"));
    // Ensure closing via the dock title-bar [x] updates the action; showing again works
    connect(m_metadataDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (m_toggleMetadataAct->isChecked() != visible) {
            QSignalBlocker blocker(m_toggleMetadataAct);
            m_toggleMetadataAct->setChecked(visible);
        }
    });

    m_thumbnailsBottomAct = new QAction(tr("Thumbnails on &Bottom"), this);
    m_thumbnailsBottomAct->setCheckable(true);
    m_thumbnailsBottomAct->setStatusTip(tr("Place the thumbnail strip along the bottom edge"));
    connect(m_thumbnailsBottomAct, &QAction::triggered, this, [this]() {
        setThumbnailBarPosition(ThumbnailEdge::Bottom);
    });

    m_thumbnailsTopAct = new QAction(tr("Thumbnails on &Top"), this);
    m_thumbnailsTopAct->setCheckable(true);
    m_thumbnailsTopAct->setStatusTip(tr("Place the thumbnail strip along the top edge"));
    connect(m_thumbnailsTopAct, &QAction::triggered, this, [this]() {
        setThumbnailBarPosition(ThumbnailEdge::Top);
    });

    m_thumbnailsLeftAct = new QAction(tr("Thumbnails on &Left"), this);
    m_thumbnailsLeftAct->setCheckable(true);
    m_thumbnailsLeftAct->setStatusTip(tr("Place the thumbnail strip along the left edge"));
    connect(m_thumbnailsLeftAct, &QAction::triggered, this, [this]() {
        setThumbnailBarPosition(ThumbnailEdge::Left);
    });

    m_thumbnailsRightAct = new QAction(tr("Thumbnails on &Right"), this);
    m_thumbnailsRightAct->setCheckable(true);
    m_thumbnailsRightAct->setStatusTip(tr("Place the thumbnail strip along the right edge"));
    connect(m_thumbnailsRightAct, &QAction::triggered, this, [this]() {
        setThumbnailBarPosition(ThumbnailEdge::Right);
    });

    m_thumbnailPositionGroup = new QActionGroup(this);
    m_thumbnailPositionGroup->setExclusive(true);
    m_thumbnailPositionGroup->addAction(m_thumbnailsBottomAct);
    m_thumbnailPositionGroup->addAction(m_thumbnailsTopAct);
    m_thumbnailPositionGroup->addAction(m_thumbnailsLeftAct);
    m_thumbnailPositionGroup->addAction(m_thumbnailsRightAct);
    m_thumbnailsBottomAct->setChecked(true);

    m_toggleScrollBarsAct = new QAction(tr("Show &Scrollbars"), this);
    m_toggleScrollBarsAct->setCheckable(true);
    m_toggleScrollBarsAct->setChecked(false);
    m_toggleScrollBarsAct->setStatusTip(tr("Show or hide scrollbars on the image view"));
    connect(m_toggleScrollBarsAct, &QAction::triggered, this, &MainWindow::toggleScrollBars);

    m_preferencesAct = new QAction(tr("&Preferences..."), this);
    m_preferencesAct->setShortcut(QKeySequence::Preferences);
    m_preferencesAct->setIcon(themeIcon(QStringLiteral("preferences-system"), QStyle::SP_FileDialogInfoView));
    m_preferencesAct->setStatusTip(tr("Application preferences"));
    connect(m_preferencesAct, &QAction::triggered, this, &MainWindow::showPreferences);

    m_aboutAct = new QAction(tr("&About QImgView"), this);
    m_aboutAct->setIcon(themeIcon(QStringLiteral("help-about"), QStyle::SP_MessageBoxInformation));
    m_aboutAct->setStatusTip(tr("About this application"));
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->addAction(m_openAct);
    m_fileMenu->addAction(m_addAct);
    m_fileMenu->addAction(m_openDirAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_quitAct);

    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    m_editMenu->addAction(m_undoAct);
    m_editMenu->addAction(m_redoAct);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_selectAllAct);
    m_editMenu->addSeparator();
    m_editMenu->addAction(m_preferencesAct);

    m_viewMenu = menuBar()->addMenu(tr("&View"));

    auto *zoomMenu = m_viewMenu->addMenu(tr("&Zoom"));
    zoomMenu->addAction(m_zoomInAct);
    zoomMenu->addAction(m_zoomOutAct);
    zoomMenu->addAction(m_zoom1to1Act);
    zoomMenu->addAction(m_zoomFitAct);
    zoomMenu->addAction(m_zoomFillAct);

    auto *imageMenu = m_viewMenu->addMenu(tr("&Image"));
    imageMenu->addAction(m_rotateLeftAct);
    imageMenu->addAction(m_rotateRightAct);
    imageMenu->addAction(m_flipHAct);
    imageMenu->addAction(m_flipVAct);

    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_toggleHudAct);
    m_viewMenu->addAction(m_fullscreenAct);
    m_viewMenu->addAction(m_toggleToolBarAct);
    m_viewMenu->addAction(m_toggleMetadataAct);
    m_viewMenu->addAction(m_toggleScrollBarsAct);

    auto *thumbsMenu = m_viewMenu->addMenu(tr("&Thumbnails"));
    thumbsMenu->addAction(m_toggleThumbnailBarAct);
    thumbsMenu->addAction(m_hideThumbLabelsAct);
    thumbsMenu->addSeparator();
    thumbsMenu->addAction(m_thumbnailsBottomAct);
    thumbsMenu->addAction(m_thumbnailsTopAct);
    thumbsMenu->addAction(m_thumbnailsLeftAct);
    thumbsMenu->addAction(m_thumbnailsRightAct);

    m_viewMenu->addSeparator();
    auto *sortMenu = m_viewMenu->addMenu(tr("&Sort"));
    sortMenu->addAction(m_sortNameAct);
    sortMenu->addAction(m_sortMTimeAct);

    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_backToGalleryAct);

    auto *galleryMenu = m_viewMenu->addMenu(tr("&Gallery"));
    galleryMenu->addAction(m_layoutSideBySideAct);
    galleryMenu->addAction(m_layoutVerticalAct);
    galleryMenu->addAction(m_layoutGridAct);
    galleryMenu->addAction(m_layoutGridCropAct);
    galleryMenu->addAction(m_layoutMasonryAct);
    galleryMenu->addAction(m_layoutMasonryRowsAct);
    galleryMenu->addAction(m_layoutStackAct);

    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_workspaceModeAct);

    auto *workspaceMenu = m_viewMenu->addMenu(tr("&Workspace"));
    workspaceMenu->addAction(m_selectToolAct);
    workspaceMenu->addAction(m_panToolAct);
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(m_raiseAct);
    workspaceMenu->addAction(m_lowerAct);
    workspaceMenu->addAction(m_opacityUpAct);
    workspaceMenu->addAction(m_opacityDownAct);
    workspaceMenu->addAction(m_opacityResetAct);
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(m_clearExtrasAct);

    m_goMenu = menuBar()->addMenu(tr("&Go"));
    m_goMenu->addAction(m_firstAct);
    m_goMenu->addAction(m_previousAct);
    m_goMenu->addAction(m_nextAct);
    m_goMenu->addAction(m_lastAct);
    m_goMenu->addSeparator();
    m_goMenu->addAction(m_slideshowAct);

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addAction(m_aboutAct);

    m_contextMenu = new QMenu(this);
    m_contextMenu->addAction(m_openAct);
    m_contextMenu->addAction(m_addAct);
    m_contextMenu->addAction(m_openDirAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_previousAct);
    m_contextMenu->addAction(m_nextAct);
    m_contextMenu->addAction(m_firstAct);
    m_contextMenu->addAction(m_lastAct);
    m_contextMenu->addAction(m_slideshowAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_zoomInAct);
    m_contextMenu->addAction(m_zoomOutAct);
    m_contextMenu->addAction(m_zoom1to1Act);
    m_contextMenu->addAction(m_zoomFitAct);
    m_contextMenu->addAction(m_zoomFillAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_rotateLeftAct);
    m_contextMenu->addAction(m_rotateRightAct);
    m_contextMenu->addAction(m_flipHAct);
    m_contextMenu->addAction(m_flipVAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_backToGalleryAct);
    m_contextMenu->addAction(m_layoutSideBySideAct);
    m_contextMenu->addAction(m_layoutVerticalAct);
    m_contextMenu->addAction(m_layoutGridAct);
    m_contextMenu->addAction(m_layoutGridCropAct);
    m_contextMenu->addAction(m_layoutMasonryAct);
    m_contextMenu->addAction(m_layoutMasonryRowsAct);
    m_contextMenu->addAction(m_layoutStackAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_workspaceModeAct);
    m_contextMenu->addAction(m_raiseAct);
    m_contextMenu->addAction(m_lowerAct);
    m_contextMenu->addAction(m_clearExtrasAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_fullscreenAct);
    m_contextMenu->addAction(m_toggleToolBarAct);
    m_contextMenu->addAction(m_toggleThumbnailBarAct);
    m_contextMenu->addAction(m_toggleMetadataAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_preferencesAct);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setFloatable(false);
    m_toolBar->setIconSize(QSize(24, 24));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Left: file + undo/redo
    // Up to gallery (enabled only after opening an image from Gallery)
    m_toolBar->addAction(m_backToGalleryAct);
    m_toolBar->addAction(m_openAct);
    m_toolBar->addAction(m_addAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_undoAct);
    m_toolBar->addAction(m_redoAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_rotateLeftAct);
    m_toolBar->addAction(m_rotateRightAct);
    m_toolBar->addAction(m_flipHAct);
    m_toolBar->addAction(m_flipVAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_layoutSideBySideAct);
    m_toolBar->addAction(m_layoutVerticalAct);
    m_toolBar->addAction(m_layoutGridAct);
    m_toolBar->addAction(m_layoutGridCropAct);
    m_toolBar->addAction(m_layoutMasonryAct);

    m_toolBar->addAction(m_layoutMasonryRowsAct);

    // Masonry column/row count — shown while a masonry layout is active
    auto *masonryCountHost = new QWidget(m_toolBar);
    auto *masonryCountLayout = new QHBoxLayout(masonryCountHost);
    masonryCountLayout->setContentsMargins(4, 0, 4, 0);
    masonryCountLayout->setSpacing(4);
    m_masonryCountLabel = new QLabel(tr("Columns:"), masonryCountHost);
    m_masonryCountSpin = new QSpinBox(masonryCountHost);
    m_masonryCountSpin->setRange(1, 32);
    m_masonryCountSpin->setSingleStep(1);
    m_masonryCountSpin->setValue(3);
    m_masonryCountSpin->setToolTip(
        tr("Number of columns or rows in Masonry layout (fits the window)."));
    masonryCountLayout->addWidget(m_masonryCountLabel);
    masonryCountLayout->addWidget(m_masonryCountSpin);
    m_masonryCountAction = m_toolBar->addWidget(masonryCountHost);
    m_masonryCountAction->setVisible(false);
    connect(m_masonryCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int count) {
                if (!m_imageView) {
                    return;
                }
                if (m_imageView->layoutMode() == ImageView::LayoutMode::MasonryRows) {
                    m_imageView->setMasonryRows(count);
                } else {
                    m_imageView->setMasonryColumns(count);
                }
            });

    m_toolBar->addAction(m_layoutStackAct);
    m_toolBar->addAction(m_raiseAct);
    m_toolBar->addAction(m_lowerAct);

    // Expanding spacer — centres prev / play / next
    auto *spacerLeft = new QWidget(m_toolBar);
    spacerLeft->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacerLeft);

    // Centre: previous, slideshow, next
    m_toolBar->addAction(m_previousAct);
    m_toolBar->addAction(m_slideshowAct);
    m_toolBar->addAction(m_nextAct);

    auto *spacerRight = new QWidget(m_toolBar);
    spacerRight->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacerRight);

    // Right: zoom group, workspace mode, metadata, fullscreen
    m_toolBar->addAction(m_zoomInAct);
    m_toolBar->addAction(m_zoomOutAct);
    m_toolBar->addAction(m_zoom1to1Act);
    m_toolBar->addAction(m_zoomFitAct);
    m_toolBar->addAction(m_zoomFillAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_workspaceModeAct);
    m_toolBar->addAction(m_toggleMetadataAct);
    m_toolBar->addAction(m_fullscreenAct);

    // Left vertical toolbar for workspace tools (hidden until workspace mode)
    m_workspaceToolBar = new QToolBar(tr("Workspace Tools"), this);
    m_workspaceToolBar->setObjectName(QStringLiteral("WorkspaceToolBar"));
    m_workspaceToolBar->setMovable(false);
    m_workspaceToolBar->setFloatable(false);
    m_workspaceToolBar->setIconSize(QSize(24, 24));
    m_workspaceToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_workspaceToolBar->setOrientation(Qt::Vertical);
    addToolBar(Qt::LeftToolBarArea, m_workspaceToolBar);
    m_workspaceToolBar->addAction(m_selectToolAct);
    m_workspaceToolBar->addAction(m_panToolAct);
    m_workspaceToolBar->addSeparator();
    m_workspaceToolBar->addAction(m_undoAct);
    m_workspaceToolBar->addAction(m_redoAct);
    m_workspaceToolBar->hide();
}

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready"));
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_colorSwatch = new QLabel;
    m_colorSwatch->setFixedSize(16, 16);
    m_colorSwatch->setToolTip(tr("Colour under the cursor"));
    m_colorSwatch->setStyleSheet(QStringLiteral(
        "QLabel { border: 1px solid #888; background: transparent; }"));
    m_mouseLabel = new QLabel;
    m_mouseLabel->setMinimumWidth(200);
    m_mouseLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_mouseLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_colorSwatch);
    statusBar()->addPermanentWidget(m_mouseLabel);
}

bool MainWindow::isImageFile(const QString &path)
{
    return ImageLoader::isImageFile(path);
}

QStringList MainWindow::expandPaths(const QStringList &paths) const
{
    QStringList images;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            QDir::Filters filters = QDir::Files | QDir::Readable | QDir::NoDotAndDotDot;
            QDirIterator::IteratorFlags flags = m_recursive
                ? QDirIterator::Subdirectories
                : QDirIterator::NoIteratorFlags;
            QDirIterator it(path, filters, flags);
            while (it.hasNext()) {
                const QString full = it.next();
                if (isImageFile(full)) {
                    images.append(full);
                }
            }
        } else if (info.isFile() && isImageFile(path)) {
            images.append(path);
        }
    }
    return images;
}

void MainWindow::sortFileList()
{
    if (m_files.size() <= 1) {
        return;
    }

    if (m_sortMode == SortMode::MTime) {
        std::stable_sort(m_files.begin(), m_files.end(), [](const QString &a, const QString &b) {
            const QFileInfo fa(a), fb(b);
            if (fa.lastModified() != fb.lastModified()) {
                return fa.lastModified() < fb.lastModified();
            }
            return QString::compare(a, b, Qt::CaseInsensitive) < 0;
        });
    } else {
        // Natural (numeric-aware) case-insensitive sort by file name
        QCollator collator;
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::stable_sort(m_files.begin(), m_files.end(),
                         [&collator](const QString &a, const QString &b) {
                             return collator.compare(QFileInfo(a).fileName(),
                                                     QFileInfo(b).fileName()) < 0;
                         });
    }
}

void MainWindow::setSortMode(SortMode mode)
{
    m_sortMode = mode;
    if (mode == SortMode::Name) {
        m_sortNameAct->setChecked(true);
    } else {
        m_sortMTimeAct->setChecked(true);
    }

    if (m_files.isEmpty()) {
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                ? m_files.at(m_currentIndex)
                                : QString();
    sortFileList();
    m_thumbnailBar->setFiles(m_files);
    if (m_workspaceMode) {
        // setFiles rebuilds the list; restore multi-select and canvas selection
        m_thumbnailBar->setWorkspaceMode(true);
        syncThumbnailWorkspaceSelection();
    }

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_files.indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    }
    m_currentIndex = -1; // force reload
    setCurrentIndex(newIndex);
    applyThumbnailVisibility();
}

void MainWindow::sortByName()
{
    setSortMode(SortMode::Name);
}

void MainWindow::sortByMTime()
{
    setSortMode(SortMode::MTime);
}

void MainWindow::applyThumbnailVisibility()
{
    bool show = m_files.size() > 1;
    if (m_forceNoThumbnails) {
        show = false;
    } else if (m_forceThumbnails) {
        show = !m_files.isEmpty();
    }
    m_thumbnailBar->setVisible(show && !isFullScreen());
    m_toggleThumbnailBarAct->setChecked(show);
    if (!isFullScreen()) {
        m_thumbnailBarVisibleBeforeFullscreen = show;
    }
}

void MainWindow::loadFiles(const QStringList &paths, int startAt)
{
    stopSlideshow();

    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        return;
    }

    m_files = images;
    sortFileList();
    m_currentIndex = -1;

    m_thumbnailBar->setFiles(m_files);
    applyThumbnailVisibility();

    int idx = startAt;
    if (idx < 0 || idx >= m_files.size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
    updateNavigationActions();
    updateWorkspaceActionVisibility();
}

void MainWindow::appendFiles(const QStringList &paths)
{
    QStringList images = expandPaths(paths);
    if (images.isEmpty()) {
        return;
    }

    const QString current = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                ? m_files.at(m_currentIndex)
                                : QString();

    // Deduplicate while preserving order of existing entries
    QSet<QString> seen(m_files.begin(), m_files.end());
    int added = 0;
    for (const QString &path : images) {
        if (!seen.contains(path)) {
            m_files.append(path);
            seen.insert(path);
            ++added;
        }
    }
    if (added == 0) {
        return;
    }

    // Paths currently on the workspace (selection must be restored after setFiles)
    const QStringList workspacePaths = m_workspaceMode ? m_imageView->itemPaths() : QStringList();

    sortFileList();
    m_thumbnailBar->setFiles(m_files);
    if (m_workspaceMode) {
        // setFiles rebuilds items; re-apply multi-select mode and canvas selection
        m_thumbnailBar->setWorkspaceMode(true);
        syncThumbnailWorkspaceSelection();
        // If the canvas was empty, selection may still be empty — restore paths we had
        if (m_thumbnailBar->selectedIndices().isEmpty() && !workspacePaths.isEmpty()) {
            QList<int> indices;
            for (const QString &path : workspacePaths) {
                const int idx = m_files.indexOf(path);
                if (idx >= 0) {
                    indices.append(idx);
                }
            }
            m_thumbnailBar->setSelectedIndices(indices);
        }
    }
    applyThumbnailVisibility();
    updateWorkspaceActionVisibility();

    int newIndex = 0;
    if (!current.isEmpty()) {
        newIndex = m_files.indexOf(current);
        if (newIndex < 0) {
            newIndex = 0;
        }
    } else {
        // Jump to the first newly added image after sort
        newIndex = 0;
    }

    if (m_workspaceMode) {
        m_currentIndex = newIndex;
        if (m_metadataPanel && newIndex >= 0 && newIndex < m_files.size()) {
            m_metadataPanel->setImagePath(m_files.at(newIndex));
        }
        updateStatus();
        updateNavigationActions();
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
        updateNavigationActions();
    }
}

void MainWindow::setCurrentIndex(int index)
{
    if (m_files.isEmpty() || index < 0 || index >= m_files.size()) {
        return;
    }
    if (index == m_currentIndex) {
        return;
    }

    m_currentIndex = index;
    m_imageView->loadImage(m_files.at(m_currentIndex));
    m_thumbnailBar->setCurrentIndex(m_currentIndex);
    if (m_metadataPanel) {
        m_metadataPanel->setImagePath(m_files.at(m_currentIndex));
    }
    updateWindowTitle();
    updateStatus();
    updateNavigationActions();
}

void MainWindow::removeSessionIndices(const QList<int> &indices)
{
    if (indices.isEmpty() || m_files.isEmpty()) {
        return;
    }

    stopSlideshow();

    QList<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    const QString currentPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                    ? m_files.at(m_currentIndex)
                                    : QString();

    // Remove highest indices first so remaining indices stay valid
    for (int i = sorted.size() - 1; i >= 0; --i) {
        const int idx = sorted.at(i);
        if (idx < 0 || idx >= m_files.size()) {
            continue;
        }
        const QString path = m_files.at(idx);
        m_files.removeAt(idx);
        if (m_workspaceMode && m_imageView) {
            m_imageView->removeWorkspacePath(path);
        }
    }

    m_thumbnailBar->setFiles(m_files);
    if (m_workspaceMode) {
        m_thumbnailBar->setWorkspaceMode(true);
        syncThumbnailWorkspaceSelection();
    }
    applyThumbnailVisibility();

    if (m_files.isEmpty()) {
        m_currentIndex = -1;
        m_imageView->clearWorkspace();
        if (m_metadataPanel) {
            m_metadataPanel->clear();
        }
        updateWindowTitle();
        updateStatus();
        updateNavigationActions();
        return;
    }

    int newIndex = 0;
    if (!currentPath.isEmpty()) {
        newIndex = m_files.indexOf(currentPath);
    }
    if (newIndex < 0) {
        // Prefer the nearest index after the first removed slot
        newIndex = qBound(0, sorted.first(), m_files.size() - 1);
    }

    if (m_workspaceMode) {
        m_currentIndex = newIndex;
        if (m_metadataPanel) {
            m_metadataPanel->setImagePath(m_files.at(newIndex));
        }
        m_thumbnailBar->setCurrentIndex(newIndex);
        updateStatus();
        updateNavigationActions();
    } else {
        m_currentIndex = -1;
        setCurrentIndex(newIndex);
    }
}

void MainWindow::updateNavigationActions()
{
    const bool hasMany = m_files.size() > 1;
    const bool hasFiles = !m_files.isEmpty();
    const bool hasItem = m_imageView && m_imageView->itemCount() > 0;

    // Session navigation is Image-mode oriented; disable in workspace mode
    // Prev/Next/slideshow are Image mode only (not Gallery, not Workspace).
    const bool imageNav = hasMany && m_imageView && m_imageView->isImageMode();
    m_previousAct->setEnabled(imageNav);
    m_nextAct->setEnabled(imageNav);
    if (m_firstAct) {
        m_firstAct->setEnabled(imageNav);
    }
    if (m_lastAct) {
        m_lastAct->setEnabled(imageNav);
    }
    m_slideshowAct->setEnabled(imageNav);
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(imageNav);
    }
    if (!imageNav) {
        stopSlideshow();
    }

    // Transform actions need a target image on the canvas
    for (QAction *act : {m_rotateLeftAct, m_rotateRightAct, m_flipHAct, m_flipVAct}) {
        if (act) {
            act->setEnabled(hasItem);
        }
    }

    // Zoom always useful when something is shown (or for empty view reset)
    for (QAction *act : {m_zoomInAct, m_zoomOutAct, m_zoom1to1Act, m_zoomFitAct, m_zoomFillAct}) {
        if (act) {
            act->setEnabled(hasItem || (!m_workspaceMode && hasFiles));
        }
    }

    if (m_selectAllAct) {
        m_selectAllAct->setEnabled(hasFiles);
    }
    if (m_hideThumbLabelsAct) {
        m_hideThumbLabelsAct->setEnabled(m_thumbnailBar != nullptr);
    }
    if (m_toggleHudAct) {
        m_toggleHudAct->setEnabled(true);
    }
}

void MainWindow::onThumbnailActivated(int index)
{
    // User interaction: pause slideshow
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(index);
}

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

void MainWindow::openFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Open Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        loadFiles(files);
    }
}

void MainWindow::addFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Add Images"),
        QString(),
        imageFileDialogFilter());
    if (!files.isEmpty()) {
        appendFiles(files);
    }
}

void MainWindow::openDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Directory"));
    if (!dir.isEmpty()) {
        loadFiles({dir});
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

void MainWindow::goPrevious()
{
    if (m_files.size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex - 1;
    if (idx < 0) {
        idx = m_files.size() - 1;
    }
    setCurrentIndex(idx);
    flashNavHud(tr("←  Previous"));
}

void MainWindow::goNext()
{
    if (m_files.size() <= 1) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    int idx = m_currentIndex + 1;
    if (idx >= m_files.size()) {
        idx = 0;
    }
    setCurrentIndex(idx);
    flashNavHud(tr("→  Next"));
}

void MainWindow::goFirst()
{
    if (m_files.isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(0);
    flashNavHud(tr("⇤  First"));
}

void MainWindow::goLast()
{
    if (m_files.isEmpty()) {
        return;
    }
    if (m_slideshowTimer->isActive() && !m_slideshowAdvancing) {
        stopSlideshow();
    }
    setCurrentIndex(m_files.size() - 1);
    flashNavHud(tr("⇥  Last"));
}

void MainWindow::setSlideshowIntervalMs(int ms)
{
    m_slideshowIntervalMs = qMax(500, ms);
    if (m_slideshowTimer->isActive()) {
        m_slideshowTimer->setInterval(m_slideshowIntervalMs);
    }
}

void MainWindow::showSlideshowCursor()
{
    if (m_slideshowCursorHidden) {
        QApplication::restoreOverrideCursor();
        m_slideshowCursorHidden = false;
    }
}

void MainWindow::hideSlideshowCursor()
{
    if (!m_slideshowTimer || !m_slideshowTimer->isActive()) {
        return;
    }
    if (!m_slideshowCursorHidden) {
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_slideshowCursorHidden = true;
    }
}

void MainWindow::armSlideshowCursorHide()
{
    if (!m_cursorHideTimer) {
        return;
    }
    showSlideshowCursor();
    m_cursorHideTimer->start();
}

void MainWindow::startSlideshow()
{
    if (m_files.size() <= 1 || m_workspaceMode) {
        m_slideshowAct->setChecked(false);
        return;
    }
    if (m_slideshowFullscreen && !isFullScreen()) {
        showFullScreen();
    }
    m_slideshowTimer->start(m_slideshowIntervalMs);
    m_slideshowAct->setChecked(true);
    m_slideshowAct->setText(tr("Stop &Slideshow"));
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop));
    qApp->installEventFilter(this);
    armSlideshowCursorHide();
    {
        QString detail;
        if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            detail = QFileInfo(m_files.at(m_currentIndex)).fileName();
            if (m_files.size() > 1) {
                detail += tr("  (%1/%2)").arg(m_currentIndex + 1).arg(m_files.size());
            }
        }
        m_imageView->flashHud(tr("▶  Slideshow"), detail);
    }
}

void MainWindow::stopSlideshow()
{
    m_slideshowTimer->stop();
    if (m_cursorHideTimer) {
        m_cursorHideTimer->stop();
    }
    showSlideshowCursor();
    qApp->removeEventFilter(this);
    m_slideshowAct->setChecked(false);
    m_slideshowAct->setText(tr("Play &Slideshow"));
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    {
        QString detail;
        if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            detail = QFileInfo(m_files.at(m_currentIndex)).fileName();
        }
        m_imageView->flashHud(tr("■  Stop"), detail);
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (m_slideshowTimer && m_slideshowTimer->isActive()) {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::HoverMove:
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel:
            armSlideshowCursorHide();
            break;
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleSlideshow()
{
    if (m_slideshowTimer->isActive()) {
        stopSlideshow();
    } else {
        startSlideshow();
    }
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

void MainWindow::setLayoutFreeForm()
{
    // Free-form is Workspace mode, not a Gallery layout.
    m_workspaceModeAct->setChecked(true);
    toggleWorkspaceMode();
}

void MainWindow::populateGalleryCanvas()
{
    if (!m_thumbnailBar || m_files.isEmpty()) {
        return;
    }
    m_thumbnailBar->selectAllThumbs();
    applyWorkspaceSelectionFromThumbnails();
}

void MainWindow::setLayoutSideBySide()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    // Gallery is independent of Workspace Mode
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::SideBySide);
    populateGalleryCanvas();
    // Re-apply after items are on the canvas
    m_imageView->enterGallery(ImageView::LayoutMode::SideBySide);
    m_galleryReturnLayout = ImageView::LayoutMode::SideBySide;
    if (m_layoutSideBySideAct) {
        m_layoutSideBySideAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutVertical()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    // Gallery is independent of Workspace Mode
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::Vertical);
    populateGalleryCanvas();
    // Re-apply after items are on the canvas
    m_imageView->enterGallery(ImageView::LayoutMode::Vertical);
    m_galleryReturnLayout = ImageView::LayoutMode::Vertical;
    if (m_layoutVerticalAct) {
        m_layoutVerticalAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutGrid()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::Grid);
    populateGalleryCanvas();
    m_imageView->enterGallery(ImageView::LayoutMode::Grid);
    m_galleryReturnLayout = ImageView::LayoutMode::Grid;
    if (m_layoutGridAct) {
        m_layoutGridAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutGridCrop()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::GridCrop);
    populateGalleryCanvas();
    m_imageView->enterGallery(ImageView::LayoutMode::GridCrop);
    m_galleryReturnLayout = ImageView::LayoutMode::GridCrop;
    if (m_layoutGridCropAct) {
        m_layoutGridCropAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutStack()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    // Gallery is independent of Workspace Mode
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::Stack);
    populateGalleryCanvas();
    // Re-apply after items are on the canvas
    m_imageView->enterGallery(ImageView::LayoutMode::Stack);
    m_galleryReturnLayout = ImageView::LayoutMode::Stack;
    if (m_layoutStackAct) {
        m_layoutStackAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutMasonry()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::Masonry);
    populateGalleryCanvas();
    m_imageView->enterGallery(ImageView::LayoutMode::Masonry);
    m_galleryReturnLayout = ImageView::LayoutMode::Masonry;
    if (m_layoutMasonryAct) {
        m_layoutMasonryAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutMasonryRows()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    m_imageView->enterGallery(ImageView::LayoutMode::MasonryRows);
    populateGalleryCanvas();
    m_imageView->enterGallery(ImageView::LayoutMode::MasonryRows);
    m_galleryReturnLayout = ImageView::LayoutMode::MasonryRows;
    if (m_layoutMasonryRowsAct) {
        m_layoutMasonryRowsAct->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::openGalleryItemInImageMode(const QString &path)
{
    if (path.isEmpty() || m_files.isEmpty()) {
        return;
    }
    const int idx = m_files.indexOf(path);
    if (idx < 0) {
        return;
    }
    if (m_imageView && m_imageView->isGalleryLayout()) {
        m_galleryReturnLayout = m_imageView->layoutMode();
        m_galleryReturnActive = true;
        m_imageView->snapshotGalleryViewport();
    }
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_imageView->setViewMode(ImageView::ViewMode::Image);
    m_thumbnailBar->setWorkspaceMode(false);
    setCurrentIndex(idx);
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::returnToGallery()
{
    if (!m_galleryReturnActive) {
        return;
    }
    m_galleryReturnActive = false;
    m_workspaceMode = false;
    m_workspaceModeAct->setChecked(false);
    m_thumbnailBar->setWorkspaceMode(false);
    const QString focusPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                  ? m_files.at(m_currentIndex)
                                  : QString();
    m_imageView->enterGallery(m_galleryReturnLayout);
    populateGalleryCanvas();
    m_imageView->enterGallery(m_galleryReturnLayout);
    m_imageView->restoreGalleryViewport(focusPath);
    switch (m_galleryReturnLayout) {
    case ImageView::LayoutMode::SideBySide:
        if (m_layoutSideBySideAct) m_layoutSideBySideAct->setChecked(true);
        break;
    case ImageView::LayoutMode::Vertical:
        if (m_layoutVerticalAct) m_layoutVerticalAct->setChecked(true);
        break;
    case ImageView::LayoutMode::Grid:
        if (m_layoutGridAct) m_layoutGridAct->setChecked(true);
        break;
    case ImageView::LayoutMode::GridCrop:
        if (m_layoutGridCropAct) m_layoutGridCropAct->setChecked(true);
        break;
    case ImageView::LayoutMode::Stack:
        if (m_layoutStackAct) m_layoutStackAct->setChecked(true);
        break;
    case ImageView::LayoutMode::Masonry:
        if (m_layoutMasonryAct) m_layoutMasonryAct->setChecked(true);
        break;
    case ImageView::LayoutMode::MasonryRows:
        if (m_layoutMasonryRowsAct) m_layoutMasonryRowsAct->setChecked(true);
        break;
    default:
        if (m_layoutMasonryAct) m_layoutMasonryAct->setChecked(true);
        break;
    }
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::updateMasonryCountControl()
{
    if (!m_imageView || !m_masonryCountAction) {
        return;
    }
    const auto mode = m_imageView->layoutMode();
    const bool columns = m_imageView->isGalleryMode()
                         && mode == ImageView::LayoutMode::Masonry;
    const bool rows = m_imageView->isGalleryMode()
                      && mode == ImageView::LayoutMode::MasonryRows;
    const bool show = columns || rows;
    m_masonryCountAction->setVisible(show);
    if (!show || !m_masonryCountSpin) {
        return;
    }
    if (m_masonryCountLabel) {
        m_masonryCountLabel->setText(rows ? tr("Rows:") : tr("Columns:"));
    }
    const QSignalBlocker blocker(m_masonryCountSpin);
    m_masonryCountSpin->setValue(rows ? m_imageView->masonryRows()
                                      : m_imageView->masonryColumns());
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


void MainWindow::updateScrollBarPolicyForMode()
{
    if (!m_imageView) {
        return;
    }
    Qt::ScrollBarPolicy h = Qt::ScrollBarAlwaysOff;
    Qt::ScrollBarPolicy v = Qt::ScrollBarAlwaysOff;
    if (m_imageView->isGalleryMode()) {
        // AlwaysOn keeps the viewport size stable so gallery packing does not
        // oscillate when AsNeeded scrollbars appear/disappear.
        h = Qt::ScrollBarAlwaysOn;
        v = Qt::ScrollBarAlwaysOn;
    } else if (m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked()) {
        h = Qt::ScrollBarAsNeeded;
        v = Qt::ScrollBarAsNeeded;
    }
    if (m_imageView->horizontalScrollBarPolicy() != h) {
        m_imageView->setHorizontalScrollBarPolicy(h);
    }
    if (m_imageView->verticalScrollBarPolicy() != v) {
        m_imageView->setVerticalScrollBarPolicy(v);
    }
}

void MainWindow::updateThumbnailBarForMode()
{
    if (!m_thumbnailBar) {
        return;
    }
    const bool gallery = m_imageView && m_imageView->isGalleryMode();
    if (gallery) {
        if (!m_thumbsHiddenForGallery) {
            m_thumbsVisibleBeforeGallery = m_thumbnailBar->isVisible();
            m_thumbsHiddenForGallery = true;
        }
        if (m_thumbnailBar->isVisible()) {
            m_thumbnailBar->setVisible(false);
        }
        if (m_toggleThumbnailBarAct) {
            m_toggleThumbnailBarAct->setChecked(false);
        }
        return;
    }

    if (m_thumbsHiddenForGallery) {
        m_thumbsHiddenForGallery = false;
        if (m_thumbsVisibleBeforeGallery) {
            m_thumbnailBar->setVisible(true);
            if (m_toggleThumbnailBarAct) {
                m_toggleThumbnailBarAct->setChecked(true);
            }
        }
    }
}

void MainWindow::updateUpToGalleryAction()
{
    if (!m_backToGalleryAct) {
        return;
    }
    // Always shown; only enabled when Image mode was entered from Gallery.
    m_backToGalleryAct->setVisible(true);
    m_backToGalleryAct->setEnabled(m_galleryReturnActive);
}

void MainWindow::updateWorkspaceActionVisibility()
{
    updateUpToGalleryAction();
    const bool gallery = m_imageView && m_imageView->isGalleryMode();
    const bool workspace = m_imageView && m_imageView->isWorkspaceMode();
    // Gallery layout actions: always visible; enabled once a session exists.
    // (Previously they stayed hidden until Workspace Mode was toggled because
    //  loadFiles never refreshed visibility.)
    const bool canGallery = !m_files.isEmpty();
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                         m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutStackAct}) {
        if (act) {
            act->setVisible(true);
            act->setEnabled(canGallery);
        }
    }
    if (m_layoutFreeFormAct) {
        m_layoutFreeFormAct->setVisible(false);
        m_layoutFreeFormAct->setEnabled(false);
    }
    // Free-form Workspace tools only (never in Gallery)
    for (QAction *act : {m_raiseAct, m_lowerAct,
                         m_opacityUpAct, m_opacityDownAct, m_opacityResetAct,
                         m_clearExtrasAct, m_selectToolAct, m_panToolAct}) {
        if (act) {
            act->setVisible(workspace);
            act->setEnabled(workspace);
        }
    }
    if (m_undoAct) {
        m_undoAct->setVisible(true);
        m_redoAct->setVisible(true);
    }
    if (m_workspaceToolBar) {
        m_workspaceToolBar->setVisible(workspace && !isFullScreen());
    }
    updateThumbnailBarForMode();
    updateScrollBarPolicyForMode();
    updateMasonryCountControl();
    updateNavigationActions();
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
