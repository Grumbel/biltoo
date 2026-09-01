// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"

void ImageView::discardStashedGallery()
{
    m_gallery.discardStash();
}

void ImageView::stashGalleryItems()
{
    m_gallery.stashItems();
}

void ImageView::restoreStashedGalleryItems()
{
    m_gallery.restoreStashedItems();
}

void ImageView::snapshotGalleryViewport()
{
    m_gallery.snapshotViewport();
}

void ImageView::restoreGalleryViewport(const QString &focusPath)
{
    m_gallery.restoreViewport(focusPath);
}

void ImageView::applyPendingGalleryRestore()
{
    m_gallery.applyPendingRestore();
}

void ImageView::reassertGalleryViewport()
{
    m_gallery.reassertViewport();
}

void ImageView::leaveForImageMode()
{
    m_gallery.leaveForImageMode();
}

void ImageView::returnToGalleryFromImage(LayoutMode layout, const QString &focusPath)
{
    m_gallery.returnFromImage(static_cast<int>(layout), focusPath);
}

void ImageView::returnToWorkspaceFromImage()
{
    setViewMode(ViewMode::Workspace);
}

void ImageView::enterGallery(LayoutMode packagedLayout)
{
    m_gallery.enter(static_cast<int>(packagedLayout));
}
