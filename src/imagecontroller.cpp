// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecontroller.h"
#include "imageview.h"
#include <QKeyEvent>

ImageController::ImageController(ImageView *view)
    : m_view(view)
{
}

QString ImageController::takeClassicPath()
{
    const QString path = m_classicPath;
    m_classicPath.clear();
    return path;
}

void ImageController::enter()
{
    // Gallery/Workspace → Image: stash already done in the matching onLeave.
    m_view->setActiveMode(ImageView::ViewMode::Image, LayoutMode::FreeForm);
    m_view->stopDeferredPacking();
    m_view->prepareImageModeCanvas();
    // Prefer explicit classic path (MainWindow pins the open target before
    // leaveForImageMode). Do not pick live Workspace/Gallery items — that
    // re-decodes a random tile before setCurrentIndex runs.
    const QString path = takeClassicPath();
    // Clear live canvas only — do not discard stashes.
    m_view->clearLiveCanvas();
    m_view->clearPendingLoads();
    m_view->clearSceneKeepingStashes();
    m_view->scheduleReplaceLoad(path);
    emit m_view->statusChanged();
}

// --- Image mode session keys (Tier 6h) ---

bool ImageController::tryKeyPressNavigate(QKeyEvent *event)
{
    // Image mode: Left/Right (and friends) navigate the session. QGraphicsView
    // would otherwise scroll the viewport when the image is zoomed or the view
    // has focus (typical in fullscreen), swallowing the QAction shortcuts.
    if (!m_view->isImageMode()
        || (event->modifiers()
            & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        return false;
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
    case Qt::Key_Backspace:
        emit m_view->navigatePreviousRequested();
        event->accept();
        return true;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
        emit m_view->navigateNextRequested();
        event->accept();
        return true;
    default:
        return false;
    }
}

