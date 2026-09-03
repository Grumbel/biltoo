#include "workspacebackgrounddialog.h"
// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mainwindow_includes.h"

void MainWindow::setLayoutFreeForm()
{
    // Free-form is Workspace mode, not a Gallery layout.
    m_workspaceModeAct->setChecked(true);
    toggleWorkspaceMode();
}

void MainWindow::populateGalleryCanvas()
{
    // DOMAIN: Gallery shows the full session list (m_session.paths()), not the previous
    // Workspace membership. Clearing the canvas first ensures Workspace-only
    // tiles do not linger when switching modes.
    // Caller must already be in Gallery (setWorkspacePaths is a no-op in Image).
    if (!m_imageView || m_session.paths().isEmpty()) {
        return;
    }
    m_imageView->setWorkspacePaths(m_session.paths(), m_session.ids());
}


void MainWindow::enterGalleryMode(ImageView::LayoutMode layout)
{
    // AUDIT M7a: slideshow ticks only make sense in Image mode.
    stopSlideshow();
    // DOMAIN: Mode := Gallery; pack all session paths with layout.
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_thumbnailBar->selectNoneThumbs();
    }
    m_galleryReturnLayout = layout;
    // setWorkspacePaths refuses Image mode. Enter Gallery first so the session
    // paths are actually scheduled; otherwise the first open after startup only
    // shows the single Image-mode item until Gallery is chosen again.
    m_imageView->enterGallery(layout);
    populateGalleryCanvas();

    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct, m_layoutGridAct,
                         m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct}) {
        if (act) {
            act->setChecked(false);
        }
    }
    QAction *check = nullptr;
    switch (layout) {
    case ImageView::LayoutMode::SideBySide:
        check = m_layoutSideBySideAct;
        break;
    case ImageView::LayoutMode::Vertical:
        check = m_layoutVerticalAct;
        break;
    case ImageView::LayoutMode::Grid:
        check = m_layoutGridAct;
        break;
    case ImageView::LayoutMode::GridCrop:
        check = m_layoutGridCropAct;
        break;
    case ImageView::LayoutMode::Masonry:
        check = m_layoutMasonryAct;
        break;
    case ImageView::LayoutMode::MasonryRows:
        check = m_layoutMasonryRowsAct;
        break;
    case ImageView::LayoutMode::MasonryFill:
        check = m_layoutMasonryFillAct;
        break;
    case ImageView::LayoutMode::MasonryRowsFill:
        check = m_layoutMasonryRowsFillAct;
        break;
    default:
        break;
    }
    if (check) {
        check->setChecked(true);
    }
    updateMasonryCountControl();
    updateWorkspaceActionVisibility();
}

void MainWindow::setLayoutSideBySide()
{
    enterGalleryMode(ImageView::LayoutMode::SideBySide);
}

void MainWindow::setLayoutVertical()
{
    enterGalleryMode(ImageView::LayoutMode::Vertical);
}

void MainWindow::setLayoutGrid()
{
    enterGalleryMode(ImageView::LayoutMode::Grid);
}

void MainWindow::setLayoutGridCrop()
{
    enterGalleryMode(ImageView::LayoutMode::GridCrop);
}

void MainWindow::setLayoutMasonry()
{
    enterGalleryMode(ImageView::LayoutMode::Masonry);
}

void MainWindow::setLayoutMasonryRows()
{
    enterGalleryMode(ImageView::LayoutMode::MasonryRows);
}

void MainWindow::setLayoutMasonryFill()
{
    enterGalleryMode(ImageView::LayoutMode::MasonryFill);
}

void MainWindow::setLayoutMasonryRowsFill()
{
    enterGalleryMode(ImageView::LayoutMode::MasonryRowsFill);
}

void MainWindow::openSessionIndexInImageMode(int sessionIndex)
{
    if (sessionIndex < 0 || sessionIndex >= m_session.size()) {
        return;
    }
    const QString path = m_session.paths().at(sessionIndex);
    const SessionImageId sid = sessionIdAt(sessionIndex);

    // Remember where Image was opened from so Up can restore that mode.
    if (m_imageView && m_imageView->isGalleryLayout()) {
        m_galleryReturnLayout = m_imageView->layoutMode();
        m_galleryReturnActive = true;
        m_workspaceReturnActive = false;
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        m_workspaceReturnActive = true;
        m_galleryReturnActive = false;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    // Pin the target *before* leaveForImageMode. ImageController::enter loads
    // classicPath; without this, a stale session-cursor path races the real
    // target and double-click on a Workspace tile can open the wrong image.
    if (m_imageView) {
        m_imageView->setClassicPath(path);
        m_imageView->setCurrentSessionId(sid);
        m_imageView->setSessionPosition(sessionIndex, m_session.size(), false);
        m_imageView->leaveForImageMode();
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_thumbnailBar->selectNoneThumbs();
    }
    setCurrentIndex(sessionIndex);
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::openSessionImageInImageMode(SessionImageId sessionId)
{
    const int idx = indexOfSessionId(sessionId);
    if (idx < 0) {
        return;
    }
    openSessionIndexInImageMode(idx);
}

void MainWindow::showPathInImageMode(const QString &path)
{
    // Path-only fallback (first match). Prefer openSessionImageInImageMode when
    // the session image id is known — duplicates share a path.
    if (path.isEmpty() || m_session.isEmpty()) {
        return;
    }
    const int idx = m_session.paths().indexOf(path);
    if (idx < 0) {
        return;
    }
    openSessionIndexInImageMode(idx);
}

void MainWindow::openGalleryItemInImageMode(const QString &path)
{
    showPathInImageMode(path);
}

void MainWindow::returnFromImageMode()
{
    if (m_workspaceReturnActive) {
        returnToWorkspace();
        return;
    }
    if (m_galleryReturnActive) {
        returnToGallery();
    }
}

void MainWindow::returnToGallery()
{
    if (!m_galleryReturnActive) {
        return;
    }
    m_galleryReturnActive = false;
    m_workspaceReturnActive = false;
    const QString focusPath = (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size())
                                  ? m_session.paths().at(m_currentIndex)
                                  : QString();
    const ImageView::LayoutMode layout = m_galleryReturnLayout;

    stopSlideshow();
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        m_thumbnailBar->selectNoneThumbs();
    }

    // Phase 3: restore arm + enter Gallery + apply pending centre in one place.
    if (m_imageView) {
        m_imageView->returnToGalleryFromImage(layout, focusPath);
    }
    populateGalleryCanvas();
    if (m_imageView) {
        m_imageView->applyPendingGalleryRestore();
        QTimer::singleShot(0, this, [this, focusPath]() {
            if (!m_imageView || !m_imageView->isGalleryMode()) {
                return;
            }
            m_imageView->restoreGalleryViewport(focusPath);
            m_imageView->applyPendingGalleryRestore();
        });
    }

    m_galleryReturnLayout = layout;
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct, m_layoutGridAct,
                         m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct}) {
        if (act) {
            act->setChecked(false);
        }
    }
    QAction *check = nullptr;
    switch (layout) {
    case ImageView::LayoutMode::SideBySide:
        check = m_layoutSideBySideAct;
        break;
    case ImageView::LayoutMode::Vertical:
        check = m_layoutVerticalAct;
        break;
    case ImageView::LayoutMode::Grid:
        check = m_layoutGridAct;
        break;
    case ImageView::LayoutMode::GridCrop:
        check = m_layoutGridCropAct;
        break;
    case ImageView::LayoutMode::Masonry:
        check = m_layoutMasonryAct;
        break;
    case ImageView::LayoutMode::MasonryRows:
        check = m_layoutMasonryRowsAct;
        break;
    case ImageView::LayoutMode::MasonryFill:
        check = m_layoutMasonryFillAct;
        break;
    case ImageView::LayoutMode::MasonryRowsFill:
        check = m_layoutMasonryRowsFillAct;
        break;
    default:
        break;
    }
    if (check) {
        check->setChecked(true);
    }
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::returnToWorkspace()
{
    if (!m_workspaceReturnActive) {
        return;
    }
    m_workspaceReturnActive = false;
    m_galleryReturnActive = false;
    // Phase 3: restore stashed free-form tiles (or durable snapshot) in one place.
    if (m_imageView) {
        m_imageView->returnToWorkspaceFromImage();
    } else {
        enterWorkspaceMode();
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(true);
    }
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::enterWorkspaceMode()
{
    // DOMAIN: Mode := Workspace; restore free objects if snapshotted.
    if (isWorkspaceMode()) {
        return;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(true);
    }
    // Reuse toggle path so toolbar / selection seeding stays consistent.
    toggleWorkspaceMode();
}

void MainWindow::applyCliViewMode(const QString &mode)
{
    const QString m = mode.trimmed().toLower();
    if (m.isEmpty()) {
        return;
    }
    if (m == QLatin1String("image") || m == QLatin1String("classic")) {
        if (isWorkspaceMode()) {
            if (m_workspaceModeAct) {
                m_workspaceModeAct->setChecked(false);
            }
            toggleWorkspaceMode();
        } else if (isGalleryMode()) {
            QString path;
            if (m_currentIndex >= 0 && m_currentIndex < m_session.paths().size()) {
                path = m_session.paths().at(m_currentIndex);
            } else if (!m_session.paths().isEmpty()) {
                path = m_session.paths().first();
            }
            if (!path.isEmpty()) {
                showPathInImageMode(path);
            } else if (m_imageView) {
                m_imageView->setViewMode(ImageView::ViewMode::Image);
                updateWorkspaceActionVisibility();
            }
        }
        return;
    }
    if (m == QLatin1String("gallery")) {
        enterGalleryMode(ImageView::LayoutMode::Masonry);
        return;
    }
    if (m == QLatin1String("workspace") || m == QLatin1String("work")) {
        // Empty Workspace by default; user places tiles or opens a project.
        enterWorkspaceMode();
        updateWorkspaceActionVisibility();
        return;
    }
}



void MainWindow::updateMasonryCountControl()
{
    if (!m_imageView || !m_masonryCountAction) {
        return;
    }
    const auto mode = m_imageView->layoutMode();
    const bool gallery = m_imageView->isGalleryMode();
    const bool masonryCols = gallery
        && (mode == ImageView::LayoutMode::Masonry
            || mode == ImageView::LayoutMode::MasonryFill);
    const bool masonryRows = gallery
        && (mode == ImageView::LayoutMode::MasonryRows
            || mode == ImageView::LayoutMode::MasonryRowsFill);
    const bool gridCols = gallery
                          && (mode == ImageView::LayoutMode::Grid
                              || mode == ImageView::LayoutMode::GridCrop);
    const bool show = masonryCols || masonryRows || gridCols;
    m_masonryCountAction->setVisible(show);
    if (!show || !m_masonryCountSpin) {
        return;
    }
    if (m_masonryCountLabel) {
        m_masonryCountLabel->setText(masonryRows ? tr("Rows:") : tr("Columns:"));
    }
    const QSignalBlocker blocker(m_masonryCountSpin);
    if (masonryRows) {
        m_masonryCountSpin->setValue(m_imageView->masonryRows());
    } else if (gridCols) {
        // Show effective columns (auto -> computed-looking default of current setting or 0 spin as min 1)
        const int g = m_imageView->gridColumns();
        m_masonryCountSpin->setValue(g > 0 ? g : 3);
    } else {
        m_masonryCountSpin->setValue(m_imageView->masonryColumns());
    }
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
    // Fullscreen chrome is owned by updateFullscreenUi; do not fight it here.
    if (isFullScreen()) {
        return;
    }
    // CLI force flags override per-mode preferences for the strip.
    if (m_forceNoThumbnails) {
        m_thumbnailBar->setVisible(false);
        if (m_toggleThumbnailBarAct) {
            m_toggleThumbnailBarAct->setChecked(false);
        }
        return;
    }
    if (m_forceThumbnails) {
        const bool show = !m_session.paths().isEmpty();
        m_thumbnailBar->setVisible(show);
        if (m_toggleThumbnailBarAct) {
            m_toggleThumbnailBarAct->setChecked(show);
        }
        m_thumbnailBarVisibleBeforeFullscreen = show;
        return;
    }

    bool show = false;
    if (m_imageView && m_imageView->isGalleryMode()) {
        show = m_thumbnailsPreferredGallery && !m_session.paths().isEmpty();
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        // Workspace default on: show strip whenever the session has images.
        show = m_thumbnailsPreferredWorkspace && !m_session.paths().isEmpty();
    } else {
        // Image mode: auto when multi-file session (legacy applyThumbnailVisibility).
        show = m_session.paths().size() > 1;
    }
    m_thumbnailBar->setVisible(show);
    if (m_toggleThumbnailBarAct) {
        m_toggleThumbnailBarAct->setChecked(show);
    }
    m_thumbnailBarVisibleBeforeFullscreen = show;
}

void MainWindow::updateLayoutPanelForMode()
{
    if (!m_layoutDock || !m_toggleLayoutPanelAct) {
        return;
    }
    const bool workspace = m_imageView && m_imageView->isWorkspaceMode();
    // Layout panel is Workspace-only: disable the toggle outside Workspace and
    // never leave the dock visible in Gallery or Image.
    m_toggleLayoutPanelAct->setEnabled(workspace);
    if (!workspace) {
        if (m_layoutDock->isVisible()) {
            m_layoutDock->setVisible(false);
        }
        if (m_toggleLayoutPanelAct->isChecked()) {
            QSignalBlocker blocker(m_toggleLayoutPanelAct);
            m_toggleLayoutPanelAct->setChecked(false);
        }
        return;
    }
    if (isFullScreen()) {
        return;
    }
    const bool show = m_layoutPreferredInWorkspace;
    if (m_layoutDock->isVisible() != show) {
        m_layoutDock->setVisible(show);
    }
    if (m_toggleLayoutPanelAct->isChecked() != show) {
        QSignalBlocker blocker(m_toggleLayoutPanelAct);
        m_toggleLayoutPanelAct->setChecked(show);
    }
}

void MainWindow::updateUpToGalleryAction()
{
    if (!m_backToGalleryAct) {
        return;
    }
    // Always shown; enabled when Image mode was entered from Gallery or Workspace.
    const bool canReturn = m_galleryReturnActive || m_workspaceReturnActive;
    m_backToGalleryAct->setVisible(true);
    m_backToGalleryAct->setEnabled(canReturn);
    if (m_workspaceReturnActive) {
        m_backToGalleryAct->setStatusTip(tr("Up to workspace"));
        m_backToGalleryAct->setToolTip(tr("Up to workspace"));
    } else {
        m_backToGalleryAct->setStatusTip(tr("Up to gallery"));
        m_backToGalleryAct->setToolTip(tr("Up to gallery"));
    }
    if (m_imageView) {
        m_imageView->setGalleryReturnAvailable(canReturn);
    }
}

void MainWindow::updateWorkspaceActionVisibility()
{
    updateUpToGalleryAction();
    const bool workspace = m_imageView && m_imageView->isWorkspaceMode();
    // Gallery layout actions: always visible; enabled once a session exists.
    // (Previously they stayed hidden until Workspace Mode was toggled because
    //  loadFiles never refreshed visibility.)
    const bool canGallery = !m_session.paths().isEmpty();
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                         m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct,
                         m_layoutMasonryFillAct, m_layoutMasonryRowsFillAct}) {
        if (act) {
            act->setVisible(true);
            act->setEnabled(canGallery);
        }
    }
    if (m_layoutFreeFormAct) {
        m_layoutFreeFormAct->setVisible(false);
        m_layoutFreeFormAct->setEnabled(false);
    }
    // Workspace tools: keep menu entries stable; grey out outside Workspace.
    // (Hiding them made the Workspace menu appear to grow/shrink with mode.)
    for (QAction *act : {m_raiseAct, m_lowerAct,
                         m_opacityUpAct, m_opacityDownAct, m_opacityResetAct,
                         m_resetScaleAct, m_resetRotationAct,
                         m_selectToolAct, m_panToolAct,
                         m_pageGuideAct, m_fitPageGuideAct,
                         m_workspaceBackgroundAct}) {
        if (act) {
            act->setVisible(true);
            act->setEnabled(workspace);
        }
    }
    // Duplicate: Workspace selection or Gallery selection (new session rows).
    if (m_duplicateAct) {
        m_duplicateAct->setVisible(true);
        const bool canDup = m_imageView
            && ((workspace && m_imageView->hasTransformTargets())
                || (m_imageView->isGalleryMode()
                    && !m_imageView->selectedPaths().isEmpty()));
        m_duplicateAct->setEnabled(canDup);
    }
    // Copy/Cut: Workspace selection only. Paste stays available (enters Workspace).
    const bool canEditTiles = workspace && m_imageView
        && m_imageView->hasTransformTargets();
    if (m_copyWorkspaceAct) {
        m_copyWorkspaceAct->setVisible(true);
        m_copyWorkspaceAct->setEnabled(canEditTiles);
    }
    if (m_cutWorkspaceAct) {
        m_cutWorkspaceAct->setVisible(true);
        m_cutWorkspaceAct->setEnabled(canEditTiles);
    }
    if (m_pasteWorkspaceAct) {
        m_pasteWorkspaceAct->setVisible(true);
        updatePasteActionEnabled();
    }
    if (m_undoAct) {
        m_undoAct->setVisible(true);
        m_redoAct->setVisible(true);
    }
    if (m_workspaceToolBar) {
        m_workspaceToolBar->setVisible(workspace && !isFullScreen());
    }
    updateThumbnailBarForMode();
    updateLayoutPanelForMode();
    updateScrollBarPolicyForMode();
    updateMasonryCountControl();
    updateNavigationActions();
}


void MainWindow::syncWorkspaceBackgroundActions()
{
    if (!m_imageView) {
        return;
    }
    const WorkspaceBackground wb = m_imageView->workspaceBackground();
    if (m_workspaceBgDefaultAct) {
        m_workspaceBgDefaultAct->setCheckable(true);
        m_workspaceBgDefaultAct->setChecked(wb.isAppDefault());
    }
}

void MainWindow::editWorkspaceBackground()
{
    if (!m_imageView) {
        return;
    }
    if (!isWorkspaceMode()) {
        enterWorkspaceMode();
    }
    WorkspaceBackgroundDialog dlg(this);
    dlg.setAppDefaultColors(
        m_imageView->backgroundColor(),
        m_imageView->backgroundColorAlt(),
        m_imageView->backgroundPattern() == ImageView::BackgroundPattern::Checkerboard);
    dlg.setBackground(m_imageView->workspaceBackground());
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    m_imageView->setWorkspaceBackground(dlg.background());
    syncWorkspaceBackgroundActions();
    markWorkspaceDirty();
    if (statusBar()) {
        const WorkspaceBackground wb = dlg.background();
        QString msg;
        switch (wb.mode) {
        case WorkspaceBackgroundMode::Solid:
            msg = tr("Workspace background: solid");
            break;
        case WorkspaceBackgroundMode::Checkerboard:
            msg = tr("Workspace background: checkerboard");
            break;
        case WorkspaceBackgroundMode::ImageTile:
            msg = tr("Workspace background: image pattern");
            break;
        case WorkspaceBackgroundMode::AppDefault:
        default:
            msg = tr("Workspace background: application default");
            break;
        }
        statusBar()->showMessage(msg, 2500);
    }
}

void MainWindow::workspaceBackgroundDefault()
{
    if (!m_imageView) {
        return;
    }
    m_imageView->clearWorkspaceBackground();
    syncWorkspaceBackgroundActions();
    markWorkspaceDirty();
    if (statusBar()) {
        statusBar()->showMessage(tr("Workspace background: application default"), 2500);
    }
}



