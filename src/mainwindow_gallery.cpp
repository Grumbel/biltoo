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


void MainWindow::setLayoutMasonry()
{
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
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

