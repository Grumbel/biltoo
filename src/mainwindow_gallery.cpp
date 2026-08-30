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
    syncCanvasFromThumbnailSelection();
}

void MainWindow::enterGalleryMode(ImageView::LayoutMode layout)
{
    // AUDIT M7a: slideshow ticks only make sense in Image mode.
    stopSlideshow();
    // DOMAIN: Mode := Gallery; pack all session paths with layout.
    m_galleryReturnActive = false;
    if (m_backToGalleryAct) {
        m_backToGalleryAct->setEnabled(false);
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
    }
    m_galleryReturnLayout = layout;
    m_imageView->enterGallery(layout);
    populateGalleryCanvas();
    // Re-apply after items are on the canvas so packing sees final geometry.
    m_imageView->enterGallery(layout);

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
    if (m_imageView && m_imageView->isGalleryLayout()) {
        m_galleryReturnLayout = m_imageView->layoutMode();
        m_galleryReturnActive = true;
        m_imageView->snapshotGalleryViewport();
    }
    if (m_workspaceModeAct) {
        m_workspaceModeAct->setChecked(false);
    }
    m_imageView->setViewMode(ImageView::ViewMode::Image);
    if (m_thumbnailBar) {
        m_thumbnailBar->setMultiSelectEnabled(false);
    }
    setCurrentIndex(idx);
    updateUpToGalleryAction();
    updateWorkspaceActionVisibility();
}

void MainWindow::openGalleryItemInImageMode(const QString &path)
{
    showPathInImageMode(path);
}

void MainWindow::returnToGallery()
{
    if (!m_galleryReturnActive) {
        return;
    }
    m_galleryReturnActive = false;
    const QString focusPath = (m_currentIndex >= 0 && m_currentIndex < m_files.size())
                                  ? m_files.at(m_currentIndex)
                                  : QString();
    enterGalleryMode(m_galleryReturnLayout);
    if (!focusPath.isEmpty()) {
        m_imageView->restoreGalleryViewport(focusPath);
        m_imageView->focusSessionPath(focusPath);
    }
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
    // Always shown; only enabled when Image mode was entered from Gallery.
    m_backToGalleryAct->setVisible(true);
    m_backToGalleryAct->setEnabled(m_galleryReturnActive);
    if (m_imageView) {
        m_imageView->setGalleryReturnAvailable(m_galleryReturnActive);
    }
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
                         m_resetScaleAct, m_resetRotationAct, m_duplicateAct,
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

