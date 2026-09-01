// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecontroller.h"
#include "imageview.h"
#include "imageitem.h"

#include <QTimer>
#include <QUndoStack>

ImageController::ImageController(ImageView *view)
    : m_view(view)
{
}

void ImageController::enter()
{
    // Gallery/Workspace → Image: stash already done in the matching onLeave.
    m_view->m_viewMode = ImageView::ViewMode::Image;
    m_view->m_layoutMode = ImageView::LayoutMode::FreeForm;
    m_view->viewport()->update();
    if (m_view->m_layoutDebounceTimer) {
        m_view->m_layoutDebounceTimer->stop();
    }
    m_view->m_applyingLayout = false;
    m_view->prepareImageModeCanvas();
    // Prefer explicit classic path. Do not pick m_items.first() when leaving
    // Gallery — that re-decodes a random tile before setCurrentIndex loads
    // the real target (double clear + decode spike).
    const QString path = m_view->m_classicPath;
    // Clear live canvas only — do not discard stashes.
    m_view->clearInteractionState();
    if (m_view->m_undoStack) {
        m_view->m_undoStack->clear();
    }
    while (!m_view->m_items.isEmpty()) {
        m_view->destroyCanvasItem(m_view->m_items.last());
    }
    m_view->m_pendingScenePos.clear();
    m_view->m_pendingWorkspacePaths.clear();
    m_view->m_pendingRestoreStates.clear();
    m_view->m_classicPath.clear();
    if (m_view->m_scene) {
        m_view->m_scene->blockSignals(true);
        m_view->m_scene->clear();
        m_view->m_scene->blockSignals(false);
    }
    m_view->m_mouseInfo = {};
    emit m_view->mouseInfoChanged(m_view->m_mouseInfo);
    if (!path.isEmpty()) {
        m_view->scheduleImageLoad(path, ImageView::LoadReplace);
    }
    emit m_view->statusChanged();
}
