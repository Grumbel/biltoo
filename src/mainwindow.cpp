// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow.h"
#include "imageview.h"
#include "thumbnailbar.h"
#include "preferencesdialog.h"
#include "metadatapanel.h"

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
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QSet>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace {

QIcon themeIcon(const QString &name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull()) {
        icon = QApplication::style()->standardIcon(fallback);
    }
    return icon;
}

const QStringList &imageSuffixes()
{
    static const QStringList suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("svg"),
        QStringLiteral("xpm"), QStringLiteral("pbm"), QStringLiteral("pgm"),
        QStringLiteral("ppm"), QStringLiteral("ico"), QStringLiteral("xbm")
    };
    return suffixes;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("QImgView"));
    setWindowIcon(QApplication::windowIcon());
    resize(1024, 768);
    setAcceptDrops(true);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_imageView = new ImageView(central);
    m_imageView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_imageView, &QWidget::customContextMenuRequested,
            this, &MainWindow::showContextMenu);
    connect(m_imageView, &ImageView::mouseInfoChanged,
            this, &MainWindow::onMouseInfoChanged);

    m_thumbnailBar = new ThumbnailBar(central);
    connect(m_thumbnailBar, &ThumbnailBar::indexActivated,
            this, &MainWindow::onThumbnailActivated);

    layout->addWidget(m_imageView, 1);
    layout->addWidget(m_thumbnailBar, 0);
    setCentralWidget(central);

    m_metadataPanel = new MetadataPanel(this);
    m_metadataDock = new QDockWidget(tr("Metadata"), this);
    m_metadataDock->setObjectName(QStringLiteral("MetadataDock"));
    m_metadataDock->setWidget(m_metadataPanel);
    m_metadataDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, m_metadataDock);
    m_metadataDock->hide();

    createActions();
    createMenus();
    createToolBar();
    createStatusBar();

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
    m_slideshowAct->setStatusTip(tr("Start or stop the slideshow (F5)"));
    connect(m_slideshowAct, &QAction::triggered, this, &MainWindow::toggleSlideshow);

    m_workspaceModeAct = new QAction(tr("&Workspace Mode"), this);
    m_workspaceModeAct->setCheckable(true);
    m_workspaceModeAct->setChecked(false);
    m_workspaceModeAct->setIcon(themeIcon(QStringLiteral("view-paged"), QStyle::SP_DesktopIcon));
    m_workspaceModeAct->setStatusTip(
        tr("Allow multiple images on the view for comparison (drag images onto the view to add them)"));
    connect(m_workspaceModeAct, &QAction::triggered, this, &MainWindow::toggleWorkspaceMode);

    m_layoutSideBySideAct = new QAction(tr("Layout Side b&y Side"), this);
    m_layoutSideBySideAct->setShortcut(Qt::CTRL | Qt::Key_Y);
    m_layoutSideBySideAct->setIcon(themeIcon(QStringLiteral("view-grid"), QStyle::SP_FileDialogListView));
    m_layoutSideBySideAct->setStatusTip(tr("Arrange workspace images in a horizontal row"));
    connect(m_layoutSideBySideAct, &QAction::triggered, this, &MainWindow::layoutSideBySide);

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

    m_toggleMetadataAct = new QAction(tr("Show &Metadata"), this);
    m_toggleMetadataAct->setShortcut(Qt::CTRL | Qt::Key_E);
    m_toggleMetadataAct->setCheckable(true);
    m_toggleMetadataAct->setChecked(false);
    m_toggleMetadataAct->setIcon(themeIcon(QStringLiteral("dialog-information"), QStyle::SP_FileDialogInfoView));
    m_toggleMetadataAct->setStatusTip(tr("Show or hide the metadata side panel"));
    connect(m_toggleMetadataAct, &QAction::triggered, this, &MainWindow::toggleMetadataPanel);
    connect(m_metadataDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        m_toggleMetadataAct->setChecked(visible);
    });

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
    m_viewMenu->addAction(m_layoutSideBySideAct);
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
    m_viewMenu->addAction(m_toggleMetadataAct);

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
    m_contextMenu->addAction(m_layoutSideBySideAct);
    m_contextMenu->addAction(m_raiseAct);
    m_contextMenu->addAction(m_lowerAct);
    m_contextMenu->addAction(m_clearExtrasAct);
    m_contextMenu->addSeparator();
    m_contextMenu->addAction(m_fullscreenAct);
    m_contextMenu->addAction(m_toggleToolBarAct);
    m_contextMenu->addAction(m_toggleThumbnailBarAct);
    m_contextMenu->addAction(m_toggleMetadataAct);
}

void MainWindow::createToolBar()
{
    m_toolBar = addToolBar(tr("Main"));
    m_toolBar->setObjectName(QStringLiteral("MainToolBar"));
    m_toolBar->setMovable(false);
    m_toolBar->setIconSize(QSize(24, 24));
    m_toolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    // Left: file
    m_toolBar->addAction(m_openAct);
    m_toolBar->addAction(m_addAct);
    m_toolBar->addSeparator();

    // Middle-left: navigation + slideshow
    m_toolBar->addAction(m_previousAct);
    m_toolBar->addAction(m_nextAct);
    m_toolBar->addAction(m_slideshowAct);
    m_toolBar->addSeparator();

    // Middle: zoom + rotate
    m_toolBar->addAction(m_zoomInAct);
    m_toolBar->addAction(m_zoomOutAct);
    m_toolBar->addAction(m_zoom1to1Act);
    m_toolBar->addAction(m_zoomFitAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_rotateLeftAct);
    m_toolBar->addAction(m_rotateRightAct);
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_layoutSideBySideAct);

    // Expanding spacer pushes fullscreen to the far right
    auto *spacer = new QWidget(m_toolBar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolBar->addWidget(spacer);

    m_toolBar->addAction(m_fullscreenAct);
}

void MainWindow::createStatusBar()
{
    m_statusLabel = new QLabel(tr("Ready"));
    m_mouseLabel = new QLabel;
    m_mouseLabel->setMinimumWidth(180);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_mouseLabel);
}

bool MainWindow::isImageFile(const QString &path)
{
    return imageSuffixes().contains(QFileInfo(path).suffix().toLower());
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
        } else if (info.isFile()) {
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

    sortFileList();
    m_thumbnailBar->setFiles(m_files);
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
    m_currentIndex = -1;
    setCurrentIndex(newIndex);
    updateNavigationActions();
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

void MainWindow::updateNavigationActions()
{
    const bool hasMany = m_files.size() > 1;
    m_previousAct->setEnabled(hasMany);
    m_nextAct->setEnabled(hasMany);
    m_slideshowAct->setEnabled(hasMany);
    if (!hasMany) {
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

void MainWindow::openFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this,
        tr("Open Images"),
        QString(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All Files (*)"));
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
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;All Files (*)"));
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

void MainWindow::startSlideshow()
{
    if (m_files.size() <= 1) {
        m_slideshowAct->setChecked(false);
        return;
    }
    m_slideshowTimer->start(m_slideshowIntervalMs);
    m_slideshowAct->setChecked(true);
    m_slideshowAct->setText(tr("Stop &Slideshow"));
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-stop"), QStyle::SP_MediaStop));
}

void MainWindow::stopSlideshow()
{
    m_slideshowTimer->stop();
    m_slideshowAct->setChecked(false);
    m_slideshowAct->setText(tr("Play &Slideshow"));
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
}

void MainWindow::toggleSlideshow()
{
    if (m_slideshowTimer->isActive()) {
        stopSlideshow();
    } else {
        startSlideshow();
    }
}

void MainWindow::layoutSideBySide()
{
    m_imageView->layoutSideBySide();
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
    if (!m_workspaceMode) {
        // Leave comparison mode: keep only the primary image
        m_imageView->clearExtras();
    }
    updateWorkspaceActionVisibility();
}

void MainWindow::updateWorkspaceActionVisibility()
{
    const bool on = m_workspaceMode;
    for (QAction *act : {m_layoutSideBySideAct, m_raiseAct, m_lowerAct,
                         m_opacityUpAct, m_opacityDownAct, m_opacityResetAct,
                         m_clearExtrasAct}) {
        if (act) {
            act->setVisible(on);
            act->setEnabled(on);
        }
    }
    // Toolbar shows layout only in workspace mode
    if (m_toolBar && m_layoutSideBySideAct) {
        // setVisible on the action hides it from the toolbar too
    }
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

void MainWindow::toggleMetadataPanel()
{
    m_metadataDock->setVisible(m_toggleMetadataAct->isChecked());
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

void MainWindow::showPreferences()
{
    PreferencesDialog dlg(this);
    dlg.setSlideshowIntervalMs(m_slideshowIntervalMs);
    dlg.setSortModeIndex(m_sortMode == SortMode::MTime ? 1 : 0);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    setSlideshowIntervalMs(dlg.slideshowIntervalMs());
    setSortMode(dlg.sortModeIndex() == 1 ? SortMode::MTime : SortMode::Name);
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
        m_toolBar->setVisible(false);
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

    m_workspaceMode = settings.value(QStringLiteral("workspaceMode"), false).toBool();
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(m_workspaceMode);
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
    settings.setValue(QStringLiteral("workspaceMode"), m_workspaceMode);
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

    // Workspace mode: drop adds images onto the view for comparison
    if (m_workspaceMode) {
        for (const QString &path : paths) {
            // Expand directories to individual files when dropped
            const QStringList expanded = expandPaths({path});
            if (expanded.isEmpty()) {
                m_imageView->addImage(path);
            } else {
                for (const QString &img : expanded) {
                    m_imageView->addImage(img);
                }
            }
        }
        event->acceptProposedAction();
        return;
    }

    // Classic mode: Shift+drop appends to the session; plain drop replaces
    if (event->modifiers() & Qt::ShiftModifier) {
        appendFiles(paths);
    } else {
        loadFiles(paths);
    }
    event->acceptProposedAction();
}
