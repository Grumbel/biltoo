// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Thin ImageView → mode-controller input forwards (Tier 6).
// Real shell input (chrome, pan, edges, zoom region, drag/drop) stays in
// imageview_input.cpp.

#include "imageview.h"

void ImageView::updateGalleryHoverAt(const QPoint &viewPos)
{
    m_gallery.updateGalleryHoverAt(viewPos);
}

bool ImageView::tryWheelGalleryZoom(QWheelEvent *event)
{
    return m_gallery.tryWheelGalleryZoom(event);
}

bool ImageView::tryWheelGalleryScroll(QWheelEvent *event)
{
    return m_gallery.tryWheelGalleryScroll(event);
}

bool ImageView::tryMousePressSlideshowSeek(QMouseEvent *event)
{
    return m_slideshow.tryMousePressSlideshowSeek(event);
}

bool ImageView::tryMousePressAttention(QMouseEvent *event)
{
    return m_attentionCtrl.tryMousePressAttention(event);
}

bool ImageView::tryMousePressCrop(QMouseEvent *event)
{
    return m_cropCtrl.tryMousePressCrop(event);
}

bool ImageView::tryMousePressGalleryRight(QMouseEvent *event)
{
    return m_gallery.tryMousePressGalleryRight(event);
}

bool ImageView::tryMousePressGalleryLeft(QMouseEvent *event)
{
    return m_gallery.tryMousePressGalleryLeft(event);
}

bool ImageView::tryMousePressWorkspaceSelect(QMouseEvent *event)
{
    return m_workspace.tryMousePressSelect(event);
}

bool ImageView::tryMouseMoveAttention(QMouseEvent *event)
{
    return m_attentionCtrl.tryMouseMoveAttention(event);
}

bool ImageView::tryMouseMoveCropDrag(QMouseEvent *event)
{
    return m_cropCtrl.tryMouseMoveCropDrag(event);
}

bool ImageView::tryMouseMoveCropHover(QMouseEvent *event)
{
    return m_cropCtrl.tryMouseMoveCropHover(event);
}

void ImageView::updateMouseMoveSlideshowSeek(QMouseEvent *event)
{
    m_slideshow.updateMouseMoveSlideshowSeek(event);
}

bool ImageView::tryMouseReleaseSlideshowSeek(QMouseEvent *event)
{
    return m_slideshow.tryMouseReleaseSlideshowSeek(event);
}

bool ImageView::tryMouseReleaseAttention(QMouseEvent *event)
{
    return m_attentionCtrl.tryMouseReleaseAttention(event);
}

bool ImageView::tryMouseReleaseCrop(QMouseEvent *event)
{
    return m_cropCtrl.tryMouseReleaseCrop(event);
}

bool ImageView::tryKeyPressAttention(QKeyEvent *event)
{
    return m_attentionCtrl.tryKeyPressAttention(event);
}

bool ImageView::tryKeyPressCrop(QKeyEvent *event)
{
    return m_cropCtrl.tryKeyPressCrop(event);
}

bool ImageView::tryKeyPressGallery(QKeyEvent *event)
{
    return m_gallery.tryKeyPressGallery(event);
}

bool ImageView::tryKeyPressDeleteSelection(QKeyEvent *event)
{
    if (m_gallery.tryKeyPressDeleteSelection(event)) {
        return true;
    }
    if (m_workspace.tryKeyPressDeleteSelection(event)) {
        return true;
    }
    return false;
}

bool ImageView::tryKeyPressImageNavigate(QKeyEvent *event)
{
    return m_image.tryKeyPressNavigate(event);
}

bool ImageView::tryKeyPressWorkspaceShear(QKeyEvent *event)
{
    return m_workspace.tryKeyPressShear(event);
}

bool ImageView::tryMousePressImageEdges(QMouseEvent *event)
{
    return m_image.tryMousePressEdges(event);
}
