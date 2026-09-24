// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Content flip / quarter-turn rotate (all modes). Pipeline bakes pixels;
// Image-mode framing and Gallery pack run as post-steps. ImageView thin routers.

#include "image/imagecontroller.h"
#include "imageview.h"
#include "imageitem.h"
#include "display/displaypipelinecontroller.h"
#include "gallery/gallerycontroller.h"
#include "view/viewframing.h"

void ImageController::flipHorizontal()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        m_view->hostDisplayPipeline().bakeItemFlip(item, true, false);
        if (m_framing.isFitMode() && m_view->isImageMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        }
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    emit m_view->statusChanged();
}

void ImageController::flipVertical()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        m_view->hostDisplayPipeline().bakeItemFlip(item, false, true);
        if (m_framing.isFitMode() && m_view->isImageMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        }
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    }
    emit m_view->statusChanged();
}

void ImageController::rotateContentByQuarterTurns(ImageItem *item, int quarterTurns)
{
    // One content-rotate path for Workspace chrome, toolbar, and keyboard.
    // DisplayPipelineController::bakeItemRotate90 composes want + ItemWorld/undo + pixels.
    // Placement scale is NOT adjusted: fitting into the pre-rotate AABB shrinks
    // non-square images on every 90°. Workspace scene units = content pixels × scale.
    if (!item || quarterTurns == 0) {
        return;
    }

    m_view->hostDisplayPipeline().bakeItemRotate90(item, quarterTurns);

    if (m_view->isImageMode()) {
        if (m_framing.isFitMode()) {
            m_view->fitItem(item, m_view->currentFitAspectMode());
        } else if (m_framing.isFillMode()) {
            m_view->fitItem(item, Qt::KeepAspectRatioByExpanding);
        }
    }
}

void ImageController::rotateLeft()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, -1);
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    } else if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    emit m_view->statusChanged();
}

void ImageController::rotateRight()
{
    const QList<ImageItem *> targets = m_view->transformTargets();
    if (targets.isEmpty()) {
        return;
    }
    for (ImageItem *item : targets) {
        rotateContentByQuarterTurns(item, 1);
    }
    if (m_view->isGalleryMode()) {
        m_view->hostGallery().applyLayout(GalleryPackReason::ContentChange);
    } else if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    emit m_view->statusChanged();
}
