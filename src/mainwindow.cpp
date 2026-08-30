// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
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
#include <QCollator>
#include <QDir>
#include <QDockWidget>
#include <QDirIterator>
#include <QDragEnterEvent>
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

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (!icon.isNull()) {
        return icon;
    }
    // Bundled custom SVG when the icon theme has no match
    const QString resource = QStringLiteral(":/icons/actions/%1.svg").arg(name);
    if (QFile::exists(resource)) {
        icon = QIcon(resource);
        if (!icon.isNull()) {
            return icon;
        }
    }
    return QApplication::style()->standardIcon(fallback);
}

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

    m_thumbnailBar = new ThumbnailBar(m_centralSplitter);
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
    m_addAct->setShortcut(Qt::CTRL | Qt::Key_A);
    m_addAct->setIcon(themeIcon(QStringLiteral("list-add"), QStyle::SP_FileDialogNewFolder));
    m_addAct->setStatusTip(tr("Add image files to the current session"));
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
    m_zoomFitAct->setShortcut(Qt::Key_F);
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
    m_rotateLeftAct->setShortcuts({Qt::CTRL | Qt::Key_L, Qt::Key_BracketLeft});
    m_rotateLeftAct->setIcon(themeIcon(QStringLiteral("object-rotate-left"), QStyle::SP_ArrowBack));
    m_rotateLeftAct->setStatusTip(tr("Rotate 90° counter-clockwise"));
    connect(m_rotateLeftAct, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_rotateRightAct = new QAction(tr("Rotate &Right"), this);
    m_rotateRightAct->setShortcuts({Qt::CTRL | Qt::Key_R, Qt::Key_BracketRight, Qt::Key_R});
    m_rotateRightAct->setIcon(themeIcon(QStringLiteral("object-rotate-right"), QStyle::SP_ArrowForward));
    m_rotateRightAct->setStatusTip(tr("Rotate 90° clockwise"));
    connect(m_rotateRightAct, &QAction::triggered, this, &MainWindow::rotateRight);

    m_previousAct = new QAction(tr("&Previous Image"), this);
    m_previousAct->setShortcuts({Qt::Key_Left, Qt::Key_Backspace, Qt::Key_PageUp});
    m_previousAct->setIcon(themeIcon(QStringLiteral("go-previous"), QStyle::SP_ArrowBack));
    m_previousAct->setStatusTip(tr("Show previous image"));
    connect(m_previousAct, &QAction::triggered, this, &MainWindow::goPrevious);

    m_nextAct = new QAction(tr("&Next Image"), this);
    m_nextAct->setShortcuts({Qt::Key_Right, Qt::Key_Space, Qt::Key_PageDown});
    m_nextAct->setIcon(themeIcon(QStringLiteral("go-next"), QStyle::SP_ArrowForward));
    m_nextAct->setStatusTip(tr("Show next image"));
    connect(m_nextAct, &QAction::triggered, this, &MainWindow::goNext);

    m_slideshowAct = new QAction(tr("Play &Slideshow"), this);
    m_slideshowAct->setShortcut(Qt::Key_F5);
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    m_slideshowAct->setCheckable(true);
    m_slideshowAct->setStatusTip(
        tr("Start or stop the slideshow (F5). Unavailable in workspace mode."));
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

    m_layoutFreeFormAct = new QAction(tr("&Free Form Layout"), this);
    m_layoutFreeFormAct->setCheckable(true);
    m_layoutFreeFormAct->setChecked(true);
    m_layoutFreeFormAct->setIcon(themeIcon(QStringLiteral("transform-move"), QStyle::SP_FileDialogDetailedView));
    m_layoutFreeFormAct->setStatusTip(tr("Place and move images freely on the workspace"));
    connect(m_layoutFreeFormAct, &QAction::triggered, this, &MainWindow::setLayoutFreeForm);

    m_layoutSideBySideAct = new QAction(tr("Layout Side b&y Side"), this);
    m_layoutSideBySideAct->setCheckable(true);
    m_layoutSideBySideAct->setShortcut(Qt::CTRL | Qt::Key_Y);
    m_layoutSideBySideAct->setIcon(themeIcon(QStringLiteral("view-split-left-right"), QStyle::SP_ArrowRight));
    m_layoutSideBySideAct->setStatusTip(tr("Arrange workspace images in a horizontal row"));
    connect(m_layoutSideBySideAct, &QAction::triggered, this, &MainWindow::setLayoutSideBySide);

    m_layoutVerticalAct = new QAction(tr("Layout &Vertical"), this);
    m_layoutVerticalAct->setCheckable(true);
    m_layoutVerticalAct->setIcon(themeIcon(QStringLiteral("view-split-top-bottom"), QStyle::SP_ArrowDown));
    m_layoutVerticalAct->setStatusTip(tr("Arrange workspace images in a vertical column"));
    connect(m_layoutVerticalAct, &QAction::triggered, this, &MainWindow::setLayoutVertical);

    m_layoutGridAct = new QAction(tr("Layout &Grid"), this);
    m_layoutGridAct->setCheckable(true);
    m_layoutGridAct->setIcon(themeIcon(QStringLiteral("view-grid"), QStyle::SP_FileDialogListView));
    m_layoutGridAct->setStatusTip(tr("Arrange workspace images in a grid"));
    connect(m_layoutGridAct, &QAction::triggered, this, &MainWindow::setLayoutGrid);

    m_layoutStackAct = new QAction(tr("Layout Stac&k"), this);
    m_layoutStackAct->setCheckable(true);
    m_layoutStackAct->setIcon(themeIcon(QStringLiteral("view-stack"), QStyle::SP_TitleBarNormalButton));
    m_layoutStackAct->setStatusTip(tr("Stack images on top of each other for opacity comparison"));
    connect(m_layoutStackAct, &QAction::triggered, this, &MainWindow::setLayoutStack);

    m_layoutMasonryAct = new QAction(tr("Layout &Masonry"), this);
    m_layoutMasonryAct->setCheckable(true);
    m_layoutMasonryAct->setIcon(themeIcon(QStringLiteral("view-full-screen"), QStyle::SP_FileDialogListView));
    m_layoutMasonryAct->setStatusTip(
        tr("Pack images into columns of equal width (Pinterest-style masonry)"));
    connect(m_layoutMasonryAct, &QAction::triggered, this, &MainWindow::setLayoutMasonry);

    auto *layoutGroup = new QActionGroup(this);
    layoutGroup->addAction(m_layoutFreeFormAct);
    layoutGroup->addAction(m_layoutSideBySideAct);
    layoutGroup->addAction(m_layoutVerticalAct);
    layoutGroup->addAction(m_layoutGridAct);
    layoutGroup->addAction(m_layoutMasonryAct);
    layoutGroup->addAction(m_layoutStackAct);
    layoutGroup->setExclusive(true);

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
        setThumbnailBarPosition(Qt::Horizontal);
    });

    m_thumbnailsLeftAct = new QAction(tr("Thumbnails on &Left"), this);
    m_thumbnailsLeftAct->setCheckable(true);
    m_thumbnailsLeftAct->setStatusTip(tr("Place the thumbnail strip along the left edge"));
    connect(m_thumbnailsLeftAct, &QAction::triggered, this, [this]() {
        setThumbnailBarPosition(Qt::Vertical);
    });

    m_thumbnailPositionGroup = new QActionGroup(this);
    m_thumbnailPositionGroup->setExclusive(true);
    m_thumbnailPositionGroup->addAction(m_thumbnailsBottomAct);
    m_thumbnailPositionGroup->addAction(m_thumbnailsLeftAct);
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
    m_fileMenu->addAction(m_preferencesAct);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_quitAct);

    m_editMenu = menuBar()->addMenu(tr("&Edit"));
    m_editMenu->addAction(m_undoAct);
    m_editMenu->addAction(m_redoAct);

    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->addAction(m_zoomInAct);
    m_viewMenu->addAction(m_zoomOutAct);
    m_viewMenu->addAction(m_zoom1to1Act);
    m_viewMenu->addAction(m_zoomFitAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_rotateLeftAct);
    m_viewMenu->addAction(m_rotateRightAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_sortNameAct);
    m_viewMenu->addAction(m_sortMTimeAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_workspaceModeAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_selectToolAct);
    m_viewMenu->addAction(m_panToolAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_layoutFreeFormAct);
    m_viewMenu->addAction(m_layoutSideBySideAct);
    m_viewMenu->addAction(m_layoutVerticalAct);
    m_viewMenu->addAction(m_layoutGridAct);
    m_viewMenu->addAction(m_layoutMasonryAct);
    m_viewMenu->addAction(m_layoutStackAct);
    m_viewMenu->addAction(m_raiseAct);
    m_viewMenu->addAction(m_lowerAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_opacityUpAct);
    m_viewMenu->addAction(m_opacityDownAct);
    m_viewMenu->addAction(m_opacityResetAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_clearExtrasAct);
    m_viewMenu->addSeparator();
    m_viewMenu->addAction(m_fullscreenAct);
    m_viewMenu->addAction(m_toggleToolBarAct);
    m_viewMenu->addAction(m_toggleThumbnailBarAct);
    m_viewMenu->addAction(m_thumbnailsBottomAct);
    m_viewMenu->addAction(m_thumbnailsLeftAct);
    m_viewMenu->addAction(m_toggleMetadataAct);
    m_viewMenu->addAction(m_toggleScrollBarsAct);

    m_goMenu = menuBar()->addMenu(tr("&Go"));
    m_goMenu->addAction(m_previousAct);
    m_goMenu->addAction(m_nextAct);
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
    m_contextMenu->addAction(m_slideshowAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_zoomInAct);
    m_contextMenu->addAction(m_zoomOutAct);
    m_contextMenu->addAction(m_zoom1to1Act);
    m_contextMenu->addAction(m_zoomFitAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_rotateLeftAct);
    m_contextMenu->addAction(m_rotateRightAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_workspaceModeAct);
    m_contextMenu->addAction(m_layoutFreeFormAct);
    m_contextMenu->addAction(m_layoutSideBySideAct);
    m_contextMenu->addAction(m_layoutVerticalAct);
    m_contextMenu->addAction(m_layoutGridAct);
    m_contextMenu->addAction(m_layoutMasonryAct);
    m_contextMenu->addAction(m_layoutStackAct);
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

    // Left: file
    m_toolBar->addAction(m_openAct);
    m_toolBar->addAction(m_addAct);
    m_toolBar->addSeparator();

    // Left: zoom and rotate
    m_toolBar->addAction(m_zoomInAct);
    m_toolBar->addAction(m_zoomOutAct);
    m_toolBar->addAction(m_zoom1to1Act);
    m_toolBar->addAction(m_zoomFitAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_rotateLeftAct);
    m_toolBar->addAction(m_rotateRightAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_layoutFreeFormAct);
    m_toolBar->addAction(m_layoutSideBySideAct);
    m_toolBar->addAction(m_layoutVerticalAct);
    m_toolBar->addAction(m_layoutGridAct);
    m_toolBar->addAction(m_layoutMasonryAct);
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

    // Right: workspace mode, metadata, fullscreen
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
    m_mouseLabel = new QLabel;
    m_mouseLabel->setMinimumWidth(200);
    m_mouseLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_mouseLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(m_statusLabel, 1);
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
    // Session navigation is Image-mode oriented; disable in workspace mode
    m_previousAct->setEnabled(hasMany && !m_workspaceMode);
    m_nextAct->setEnabled(hasMany && !m_workspaceMode);
    m_slideshowAct->setEnabled(hasMany && !m_workspaceMode);
    if (m_imageView) {
        m_imageView->setImageModeNavigationEnabled(hasMany && !m_workspaceMode);
    }
    if (!hasMany || m_workspaceMode) {
        stopSlideshow();
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

void MainWindow::setLayoutFreeForm()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::FreeForm);
}

void MainWindow::setLayoutSideBySide()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::SideBySide);
}

void MainWindow::setLayoutVertical()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::Vertical);
}

void MainWindow::setLayoutGrid()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::Grid);
}

void MainWindow::setLayoutStack()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::Stack);
}

void MainWindow::setLayoutMasonry()
{
    m_imageView->setLayoutMode(ImageView::LayoutMode::Masonry);
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
    m_workspaceMode = m_workspaceModeAct->isChecked();
    m_imageView->setWorkspaceMode(m_workspaceMode);
    m_thumbnailBar->setWorkspaceMode(m_workspaceMode);
    if (m_workspaceMode) {
        // Seed the workspace with the current session image (or restore canvas)
        if (m_imageView->itemCount() == 0
            && m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
            m_imageView->addImage(m_files.at(m_currentIndex));
        }
        syncThumbnailWorkspaceSelection();
        // Ensure thumbnail bar is visible when working with multi-select
        if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
    } else {
        // Ensure classic view shows the current session image, centred
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

void MainWindow::updateWorkspaceActionVisibility()
{
    const bool on = m_workspaceMode;
    for (QAction *act : {m_layoutFreeFormAct, m_layoutSideBySideAct,
                         m_layoutVerticalAct, m_layoutGridAct, m_layoutMasonryAct,
                         m_layoutStackAct,
                         m_raiseAct, m_lowerAct,
                         m_opacityUpAct, m_opacityDownAct, m_opacityResetAct,
                         m_clearExtrasAct, m_selectToolAct, m_panToolAct}) {
        if (act) {
            act->setVisible(on);
            act->setEnabled(on);
        }
    }
    // Undo/Redo stay in Edit menu always; enabled by the undo stack itself.
    // Still show them on the workspace tool strip only in workspace mode.
    if (m_undoAct) {
        m_undoAct->setVisible(true);
        m_redoAct->setVisible(true);
    }
    if (m_workspaceToolBar) {
        m_workspaceToolBar->setVisible(on && !isFullScreen());
    }
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

void MainWindow::setThumbnailBarPosition(Qt::Orientation orientation)
{
    if (!m_centralSplitter || !m_thumbnailBar || !m_imageView) {
        return;
    }

    const bool horizontalBar = (orientation == Qt::Horizontal);
    if (m_thumbnailBar->barOrientation() == orientation
        && m_centralSplitter->orientation()
               == (horizontalBar ? Qt::Vertical : Qt::Horizontal)) {
        if (m_thumbnailsBottomAct) {
            m_thumbnailsBottomAct->setChecked(horizontalBar);
        }
        if (m_thumbnailsLeftAct) {
            m_thumbnailsLeftAct->setChecked(!horizontalBar);
        }
        return;
    }

    const int thumbSize = m_thumbnailBar->thumbSize();
    const bool barVisible = m_thumbnailBar->isVisible();

    m_thumbnailBar->setBarOrientation(orientation);

    // Re-parent widgets into the splitter in the right order
    m_imageView->setParent(nullptr);
    m_thumbnailBar->setParent(nullptr);
    while (m_centralSplitter->count() > 0) {
        m_centralSplitter->widget(0)->setParent(nullptr);
    }

    if (horizontalBar) {
        // Image on top, thumbnails along the bottom
        m_centralSplitter->setOrientation(Qt::Vertical);
        m_centralSplitter->addWidget(m_imageView);
        m_centralSplitter->addWidget(m_thumbnailBar);
        m_centralSplitter->setStretchFactor(0, 1);
        m_centralSplitter->setStretchFactor(1, 0);
        m_imageView->setMinimumHeight(120);
        m_imageView->setMinimumWidth(0);
        const int barExtent = ThumbnailBar::extentForThumbSize(thumbSize);
        m_centralSplitter->setSizes({qMax(200, height() - barExtent - 80), barExtent});
    } else {
        // Thumbnails on the left, image on the right
        m_centralSplitter->setOrientation(Qt::Horizontal);
        m_centralSplitter->addWidget(m_thumbnailBar);
        m_centralSplitter->addWidget(m_imageView);
        m_centralSplitter->setStretchFactor(0, 0);
        m_centralSplitter->setStretchFactor(1, 1);
        m_imageView->setMinimumWidth(120);
        m_imageView->setMinimumHeight(0);
        const int barExtent = ThumbnailBar::extentForThumbSize(thumbSize);
        m_centralSplitter->setSizes({barExtent, qMax(200, width() - barExtent - 40)});
    }

    m_thumbnailBar->setThumbSize(thumbSize);
    m_thumbnailBar->setVisible(barVisible);

    if (m_thumbnailsBottomAct) {
        m_thumbnailsBottomAct->setChecked(horizontalBar);
    }
    if (m_thumbnailsLeftAct) {
        m_thumbnailsLeftAct->setChecked(!horizontalBar);
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
           "<p>Shortcuts: <b>F11</b> fullscreen, <b>Esc</b> leave fullscreen, "
           "<b>Ctrl+T</b> toolbar, <b>F5</b> slideshow.</p>"));
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
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    setSlideshowIntervalMs(dlg.slideshowIntervalMs());
    m_slideshowFullscreen = dlg.slideshowFullscreen();
    setSortMode(dlg.sortModeIndex() == 1 ? SortMode::MTime : SortMode::Name);
    m_startInWorkspaceMode = dlg.startInWorkspaceMode();
    writeSettings();
}

void MainWindow::updateStatus()
{
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
        setThumbnailBarPosition(pos == QLatin1String("left") ? Qt::Vertical
                                                             : Qt::Horizontal);
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
    // Persist startup preference only — not the live session toggle
    settings.setValue(QStringLiteral("startInWorkspaceMode"), m_startInWorkspaceMode);
    settings.remove(QStringLiteral("workspaceMode"));
    settings.setValue(QStringLiteral("scrollBarsVisible"),
                      m_toggleScrollBarsAct && m_toggleScrollBarsAct->isChecked());
    if (m_thumbnailBar) {
        settings.setValue(QStringLiteral("thumbnailSize"), m_thumbnailBar->thumbSize());
        settings.setValue(QStringLiteral("thumbnailBarPosition"),
                          m_thumbnailBar->barOrientation() == Qt::Vertical
                              ? QStringLiteral("left")
                              : QStringLiteral("bottom"));
    }
    if (m_centralSplitter) {
        settings.setValue(QStringLiteral("centralSplitter"), m_centralSplitter->saveState());
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

void MainWindow::dropEvent(QDropEvent *event)
{
    const QStringList paths = extractLocalImagePaths(event->mimeData());
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
        for (const QString &img : expanded) {
            m_imageView->addImage(img);
        }
        syncThumbnailWorkspaceSelection();
        // Ensure the bar is visible once the session has multiple images
        if (m_files.size() > 1 && !m_thumbnailBar->isVisible()) {
            m_toggleThumbnailBarAct->setChecked(true);
            m_thumbnailBar->setVisible(true);
        }
        event->acceptProposedAction();
        return;
    }

    // Classic mode: Shift or Ctrl+drop appends to the session; plain drop replaces
    if (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier)) {
        appendFiles(paths);
    } else {
        loadFiles(paths);
    }
    event->acceptProposedAction();
}
