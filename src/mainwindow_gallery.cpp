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
    // Gallery shows the whole session. Do not drive that through thumbnail
    // multi-select — selectAllThumbs() left the strip in MultiSelection with
    // every item highlighted, which survived into Image mode.
    // Caller must already be in Gallery (setWorkspacePaths is a no-op in Image).
    if (!m_imageView || m_files.isEmpty()) {
        return;
    }
    m_imageView->setWorkspacePaths(m_files);
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
                         m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct}) {
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

void MainWindow::showPathInImageMode(const QString &path)
{
    // DOMAIN: enter Image on path; session current := path.
    if (path.isEmpty() || m_files.isEmpty()) {
        return;
    }
    const int idx = m_files.indexOf(path);
    if (idx < 0) {
        return;
    }
    // Remember where Image was opened from so Up can restore that mode.
    if (m_imageView && m_imageView->isGalleryLayout()) {
        m_galleryReturnLayout = m_imageView->layoutMode();
        m_galleryReturnActive = true;
        m_workspaceReturnActive = false;
        m_imageView->snapshotGalleryViewport();
    } else if (m_imageView && m_imageView->isWorkspaceMode()) {
        m_workspaceReturnActive = true;
        m_galleryReturnActive = false;
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    // Single Image-mode decode: setCurrentIndex → loadImage after the canvas is empty.
    m_imageView->setViewMode(ImageView::ViewMode::Image);
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
        // setMultiSelectEnabled is a no-op when already off; still clear any
        // residual multi-highlight left by gallery packing or Select All.
        m_thumbnailBar->selectNoneThumbs();
    }
    setCurrentIndex(idx);
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
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
    const QString focusPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                  ? m_files.at(m_currentIndex)
                                  : QString();
    // Arm restore before re-enter so every applyLayout re-centres on the
    // snapshotted scene point while session images load back onto the canvas.
    if (m_imageView) {
        m_imageView->restoreGalleryViewport(focusPath);
    }
    enterGalleryMode(m_galleryReturnLayout);
    if (m_imageView) {
        m_imageView->applyPendingGalleryRestore();
        // Thumb bar hide + scrollbar AlwaysOn resize after mode switch; pack
        // again on the settled viewport, then re-apply the scene centre.
        QTimer::singleShot(0, this, [this, focusPath]() {
            if (!m_imageView || !m_imageView->isGalleryMode()) {
                return;
            }
            m_imageView->restoreGalleryViewport(focusPath);
            m_imageView->applyPendingGalleryRestore();
        });
    }
}

void MainWindow::returnToWorkspace()
{
    if (!m_workspaceReturnActive) {
        return;
    }
    m_workspaceReturnActive = false;
    m_galleryReturnActive = false;
    // setViewMode(Workspace) from Image restores stashed free-form tiles
    // (or the durable snapshot if the stash was discarded).
    enterWorkspaceMode();
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
            if (m_currentIndex >= 0 && m_currentIndex < m_files.size()) {
                path = m_files.at(m_currentIndex);
            } else if (!m_files.isEmpty()) {
                path = m_files.first();
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
        enterWorkspaceMode();
        if (m_imageView && !m_files.isEmpty()) {
            m_imageView->setWorkspacePaths(m_files);
        }
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
    const bool masonryCols = gallery && mode == ImageView::LayoutMode::Masonry;
    const bool masonryRows = gallery && mode == ImageView::LayoutMode::MasonryRows;
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
    const bool canGallery = !m_files.isEmpty();
    for (QAction *act : {m_layoutSideBySideAct, m_layoutVerticalAct,
                         m_layoutGridAct, m_layoutGridCropAct, m_layoutMasonryAct, m_layoutMasonryRowsAct}) {
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
                         m_resetScaleAct, m_resetRotationAct, m_duplicateAct,
                         m_selectToolAct, m_panToolAct}) {
        if (act) {
            act->setVisible(true);
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

