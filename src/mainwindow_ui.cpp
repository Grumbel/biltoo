// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"

void MainWindow::createActions()
{
    m_openAct = new QAction(tr("&Open..."), this);
    m_openAct->setShortcut(QKeySequence::Open);
    m_openAct->setIcon(themeIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton));
    m_openAct->setStatusTip(tr("Open image files (replace session)"));
    connect(m_openAct, &QAction::triggered, this, &MainWindow::openFiles);

    m_newAct = new QAction(tr("&New"), this);
    m_newAct->setShortcut(QKeySequence::New);
    m_newAct->setIcon(themeIcon(QStringLiteral("document-new"), QStyle::SP_FileDialogNewFolder));
    m_newAct->setStatusTip(tr("Start a new empty session"));
    connect(m_newAct, &QAction::triggered, this, &MainWindow::newSession);

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

    m_zoomRegionAct = new QAction(tr("Zoom to &Region…"), this);
    m_zoomRegionAct->setShortcut(Qt::Key_Z);
    m_zoomRegionAct->setStatusTip(
        tr("Drag a rectangle to zoom into that area (one-shot; Esc cancels)"));
    connect(m_zoomRegionAct, &QAction::triggered, this, [this]() {
        if (m_imageView) {
            m_imageView->armZoomRegion();
        }
    });

    m_fullscreenAct = new QAction(tr("F&ullscreen"), this);
    // Keyboard F/F11 are QShortcut ApplicationShortcuts in MainWindow so leave
    // fullscreen cannot get stuck when the checkable action and window state
    // briefly disagree. Menu/toolbar still use this action.
    m_fullscreenAct->setIcon(themeIcon(QStringLiteral("view-fullscreen"), QStyle::SP_TitleBarMaxButton));
    m_fullscreenAct->setCheckable(true);
    m_fullscreenAct->setStatusTip(tr("Toggle fullscreen mode (F or F11)"));
    connect(m_fullscreenAct, &QAction::triggered, this, &MainWindow::toggleFullscreen);

    m_rotateLeftAct = new QAction(tr("Rotate &Left"), this);
    m_rotateLeftAct->setShortcut(Qt::CTRL | Qt::Key_L);
    m_rotateLeftAct->setIcon(themeIcon(QStringLiteral("object-rotate-left"), QStyle::SP_ArrowBack));
    m_rotateLeftAct->setStatusTip(tr("Rotate 90° counter-clockwise"));
    connect(m_rotateLeftAct, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_rotateRightAct = new QAction(tr("Rotate &Right"), this);
    m_rotateRightAct->setShortcuts({Qt::CTRL | Qt::Key_R, Qt::Key_R});
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
    m_slideshowAct->setShortcutContext(Qt::ApplicationShortcut);
    m_slideshowAct->setIcon(themeIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay));
    m_slideshowAct->setCheckable(true);
    m_slideshowAct->setStatusTip(
        tr("Start or stop the slideshow (Space). Unavailable in workspace mode."));
    connect(m_slideshowAct, &QAction::triggered, this, &MainWindow::toggleSlideshow);

    m_slideshowFasterAct = new QAction(tr("Slideshow &Faster"), this);
    m_slideshowFasterAct->setShortcut(Qt::Key_BracketRight);
    m_slideshowFasterAct->setShortcutContext(Qt::ApplicationShortcut);
    m_slideshowFasterAct->setStatusTip(
        tr("Shorten the slideshow interval (faster). Shortcut: ]"));
    connect(m_slideshowFasterAct, &QAction::triggered, this, &MainWindow::slideshowFaster);

    m_slideshowSlowerAct = new QAction(tr("Slideshow S&lower"), this);
    m_slideshowSlowerAct->setShortcut(Qt::Key_BracketLeft);
    m_slideshowSlowerAct->setShortcutContext(Qt::ApplicationShortcut);
    m_slideshowSlowerAct->setStatusTip(
        tr("Lengthen the slideshow interval (slower). Shortcut: ["));
    connect(m_slideshowSlowerAct, &QAction::triggered, this, &MainWindow::slideshowSlower);

    m_workspaceModeAct = new QAction(tr("&Workspace Mode"), this);
    m_workspaceModeAct->setCheckable(true);
    m_workspaceModeAct->setChecked(false);
    m_workspaceModeAct->setIcon(themeIcon(QStringLiteral("view-paged"), QStyle::SP_DesktopIcon));
    m_workspaceModeAct->setStatusTip(tr("Free-form canvas for comparing images"));
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
    layoutGroup->setExclusive(true);
    // No default checked gallery layout until the user chooses one.
    for (QAction *act : layoutGroup->actions()) {
        act->setChecked(false);
    }

    m_raiseAct = new QAction(tr("&Raise"), this);
    m_raiseAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Up);
    m_raiseAct->setIcon(themeIcon(QStringLiteral("go-up"), QStyle::SP_ArrowUp));
    m_raiseAct->setStatusTip(tr("Raise above the next overlapping image"));
    connect(m_raiseAct, &QAction::triggered, this, &MainWindow::raiseSelected);

    m_lowerAct = new QAction(tr("&Lower"), this);
    m_lowerAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Down);
    m_lowerAct->setIcon(themeIcon(QStringLiteral("go-down"), QStyle::SP_ArrowDown));
    m_lowerAct->setStatusTip(tr("Lower below the next overlapping image"));
    connect(m_lowerAct, &QAction::triggered, this, &MainWindow::lowerSelected);

    m_opacityUpAct = new QAction(tr("Opacity &Up"), this);
    // Ctrl+Shift+= / Ctrl+Shift+- — avoid clashing with Zoom In/Out (Ctrl++/Ctrl+-)
    m_opacityUpAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal));
    m_opacityUpAct->setStatusTip(tr("Increase opacity of the selected image (Ctrl+Shift+=)"));
    connect(m_opacityUpAct, &QAction::triggered, this, &MainWindow::opacityUp);

    m_opacityDownAct = new QAction(tr("Opacity &Down"), this);
    m_opacityDownAct->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus);
    m_opacityDownAct->setStatusTip(tr("Decrease opacity of the selected image (Ctrl+Shift+-)"));
    connect(m_opacityDownAct, &QAction::triggered, this, &MainWindow::opacityDown);

    m_opacityResetAct = new QAction(tr("Opacity &Reset"), this);
    m_opacityResetAct->setStatusTip(tr("Reset opacity of the selected image to 100%"));
    connect(m_opacityResetAct, &QAction::triggered, this, &MainWindow::opacityReset);

    m_resetScaleAct = new QAction(tr("Reset Item &Scale"), this);
    m_resetScaleAct->setStatusTip(tr("Reset scale of the selected image(s) to 100%"));
    connect(m_resetScaleAct, &QAction::triggered, this, &MainWindow::resetItemScale);

    m_resetRotationAct = new QAction(tr("Reset Item &Rotation"), this);
    m_resetRotationAct->setStatusTip(tr("Reset rotation of the selected image(s) to 0°"));
    connect(m_resetRotationAct, &QAction::triggered, this, &MainWindow::resetItemRotation);

    m_duplicateAct = new QAction(tr("&Duplicate"), this);
    m_duplicateAct->setShortcut(Qt::CTRL | Qt::Key_D);
    m_duplicateAct->setStatusTip(tr("Duplicate selected workspace image(s) for side-by-side comparison"));
    connect(m_duplicateAct, &QAction::triggered, this, &MainWindow::duplicateSelected);

    m_openSelectionNewWindowAct = new QAction(tr("Open Selection in &New Window"), this);
    m_openSelectionNewWindowAct->setStatusTip(
        tr("Open the selected images in a new QImgView window"));
    connect(m_openSelectionNewWindowAct, &QAction::triggered,
            this, &MainWindow::openSelectionInNewWindow);

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

    m_sortFileSizeAct = new QAction(tr("Sort by &File Size"), this);
    m_sortFileSizeAct->setCheckable(true);
    m_sortFileSizeAct->setIcon(themeIcon(QStringLiteral("document-properties"), QStyle::SP_FileDialogInfoView));
    m_sortFileSizeAct->setStatusTip(tr("Sort images by file size on disk"));
    connect(m_sortFileSizeAct, &QAction::triggered, this, &MainWindow::sortByFileSize);

    m_sortWidthAct = new QAction(tr("Sort by &Width"), this);
    m_sortWidthAct->setCheckable(true);
    m_sortWidthAct->setIcon(themeIcon(QStringLiteral("zoom-fit-width"), QStyle::SP_ArrowRight));
    m_sortWidthAct->setStatusTip(tr("Sort images by pixel width"));
    connect(m_sortWidthAct, &QAction::triggered, this, &MainWindow::sortByWidth);

    m_sortHeightAct = new QAction(tr("Sort by &Height"), this);
    m_sortHeightAct->setCheckable(true);
    m_sortHeightAct->setIcon(themeIcon(QStringLiteral("zoom-fit-height"), QStyle::SP_ArrowDown));
    m_sortHeightAct->setStatusTip(tr("Sort images by pixel height"));
    connect(m_sortHeightAct, &QAction::triggered, this, &MainWindow::sortByHeight);

    m_sortPixelCountAct = new QAction(tr("Sort by Image &Size"), this);
    m_sortPixelCountAct->setCheckable(true);
    m_sortPixelCountAct->setIcon(themeIcon(QStringLiteral("image-x-generic"), QStyle::SP_FileIcon));
    m_sortPixelCountAct->setStatusTip(tr("Sort images by pixel count (width × height)"));
    connect(m_sortPixelCountAct, &QAction::triggered, this, &MainWindow::sortByPixelCount);

    m_sortGroup = new QActionGroup(this);
    m_sortGroup->addAction(m_sortNameAct);
    m_sortGroup->addAction(m_sortMTimeAct);
    m_sortGroup->addAction(m_sortFileSizeAct);
    m_sortGroup->addAction(m_sortWidthAct);
    m_sortGroup->addAction(m_sortHeightAct);
    m_sortGroup->addAction(m_sortPixelCountAct);
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

    m_keyboardShortcutsAct = new QAction(tr("&Keyboard Shortcuts…"), this);
    m_keyboardShortcutsAct->setShortcut(Qt::Key_F1);
    m_keyboardShortcutsAct->setStatusTip(tr("List of keyboard shortcuts"));
    connect(m_keyboardShortcutsAct, &QAction::triggered, this, &MainWindow::showKeyboardShortcuts);

    m_aboutAct = new QAction(tr("&About QImgView"), this);
    m_aboutAct->setIcon(themeIcon(QStringLiteral("help-about"), QStyle::SP_MessageBoxInformation));
    m_aboutAct->setStatusTip(tr("About this application"));
    connect(m_aboutAct, &QAction::triggered, this, &MainWindow::about);
}

void MainWindow::createMenus()
{
    m_fileMenu = menuBar()->addMenu(tr("&File"));
    m_fileMenu->addAction(m_newAct);
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
    zoomMenu->addAction(m_zoomRegionAct);

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

    auto *sortMenu = m_viewMenu->addMenu(tr("&Sort"));
    sortMenu->addAction(m_sortNameAct);
    sortMenu->addAction(m_sortMTimeAct);
    sortMenu->addAction(m_sortFileSizeAct);
    sortMenu->addAction(m_sortWidthAct);
    sortMenu->addAction(m_sortHeightAct);
    sortMenu->addAction(m_sortPixelCountAct);

    // Top-level Gallery and Workspace — not buried under View.
    auto *galleryMenu = menuBar()->addMenu(tr("&Gallery"));
    galleryMenu->addAction(m_backToGalleryAct);
    galleryMenu->addSeparator();
    galleryMenu->addAction(m_layoutSideBySideAct);
    galleryMenu->addAction(m_layoutVerticalAct);
    galleryMenu->addAction(m_layoutGridAct);
    galleryMenu->addAction(m_layoutGridCropAct);
    galleryMenu->addAction(m_layoutMasonryAct);
    galleryMenu->addAction(m_layoutMasonryRowsAct);

    auto *workspaceMenu = menuBar()->addMenu(tr("&Workspace"));
    workspaceMenu->addAction(m_workspaceModeAct);
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(m_selectToolAct);
    workspaceMenu->addAction(m_panToolAct);
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(m_raiseAct);
    workspaceMenu->addAction(m_lowerAct);
    workspaceMenu->addAction(m_opacityUpAct);
    workspaceMenu->addAction(m_opacityDownAct);
    workspaceMenu->addAction(m_opacityResetAct);
    workspaceMenu->addAction(m_resetScaleAct);
    workspaceMenu->addAction(m_resetRotationAct);
    workspaceMenu->addAction(m_duplicateAct);
    workspaceMenu->addSeparator();
    workspaceMenu->addAction(m_clearExtrasAct);

    m_goMenu = menuBar()->addMenu(tr("&Go"));
    m_goMenu->addAction(m_firstAct);
    m_goMenu->addAction(m_previousAct);
    m_goMenu->addAction(m_nextAct);
    m_goMenu->addAction(m_lastAct);
    m_goMenu->addSeparator();
    m_goMenu->addAction(m_slideshowAct);
    m_goMenu->addAction(m_slideshowFasterAct);
    m_goMenu->addAction(m_slideshowSlowerAct);

    m_helpMenu = menuBar()->addMenu(tr("&Help"));
    m_helpMenu->addAction(m_keyboardShortcutsAct);
    m_helpMenu->addSeparator();
    m_helpMenu->addAction(m_aboutAct);
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
    {
        auto *sortBtn = new QToolButton(m_toolBar);
        sortBtn->setObjectName(QStringLiteral("SortToolButton"));
        sortBtn->setIcon(themeIcon(QStringLiteral("view-sort-ascending"), QStyle::SP_ArrowDown));
        sortBtn->setToolTip(tr("Sort session"));
        sortBtn->setPopupMode(QToolButton::InstantPopup);
        auto *sortPopup = new QMenu(sortBtn);
        sortPopup->addAction(m_sortNameAct);
        sortPopup->addAction(m_sortMTimeAct);
        sortPopup->addAction(m_sortFileSizeAct);
        sortPopup->addAction(m_sortWidthAct);
        sortPopup->addAction(m_sortHeightAct);
        sortPopup->addAction(m_sortPixelCountAct);
        sortBtn->setMenu(sortPopup);
        m_toolBar->addWidget(sortBtn);
    }
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
    // Workspace mode sits with the gallery layout group (mode switchers together).
    m_toolBar->addSeparator();
    m_toolBar->addAction(m_workspaceModeAct);

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
        tr("Columns (Grid / Masonry) or rows (Masonry Rows)."));
    masonryCountLayout->addWidget(m_masonryCountLabel);
    masonryCountLayout->addWidget(m_masonryCountSpin);
    m_masonryCountAction = m_toolBar->addWidget(masonryCountHost);
    m_masonryCountAction->setVisible(false);
    connect(m_masonryCountSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int count) {
                if (!m_imageView) {
                    return;
                }
                const auto mode = m_imageView->layoutMode();
                if (mode == ImageView::LayoutMode::MasonryRows) {
                    m_imageView->setMasonryRows(count);
                } else if (mode == ImageView::LayoutMode::Grid
                           || mode == ImageView::LayoutMode::GridCrop) {
                    m_imageView->setGridColumns(count);
                } else {
                    m_imageView->setMasonryColumns(count);
                }
            });

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
    m_toolBar->addAction(m_toggleThumbnailBarAct);
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
    // Select / Pan only — Undo/Redo live on the main toolbar; Raise/Lower and
    // Duplicate are available on item chrome / View menu / context menu.
    m_workspaceToolBar->addAction(m_selectToolAct);
    m_workspaceToolBar->addAction(m_panToolAct);
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


void MainWindow::bindViewerShortcuts()
{
    // In fullscreen the menu bar and toolbars are hidden. QAction shortcuts with
    // the default WindowShortcut context only fire reliably when the action is
    // reachable through a visible widget (menu/toolbar). Associate every
    // shortcut-bearing action with the main window and use ApplicationShortcut
    // so H, Space, Ctrl+F, Ctrl+R, zoom, navigation, etc. keep working while
    // the image view has focus.
    //
    // F/F11 are owned by dedicated QShortcuts in MainWindow (not the action),
    // to avoid checkable-action desync on leave-fullscreen.
    for (QAction *act : findChildren<QAction *>()) {
        if (!act || act->shortcuts().isEmpty()) {
            continue;
        }
        addAction(act);
        act->setShortcutContext(Qt::ApplicationShortcut);
    }
}
