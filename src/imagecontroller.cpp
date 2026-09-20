// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imagecontroller.h"
#include "imageview.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>

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


// --- Image mode edge chrome (Tier 6i) ---

bool ImageController::tryMousePressEdges(QMouseEvent *event)
{
    if (!m_view->isImageMode() || event->button() != Qt::LeftButton
        || (event->modifiers() & (Qt::AltModifier | Qt::ShiftModifier | Qt::ControlModifier))) {
        return false;
    }
    const ImageView::EdgeZone zone = m_view->edgeZoneAt(event->pos());
    if (zone == ImageView::EdgeZone::GalleryReturn) {
        emit m_view->galleryReturnRequested();
        event->accept();
        return true;
    }
    if (zone == ImageView::EdgeZone::Previous) {
        emit m_view->navigatePreviousRequested();
        event->accept();
        return true;
    }
    if (zone == ImageView::EdgeZone::Next) {
        emit m_view->navigateNextRequested();
        event->accept();
        return true;
    }
    // Slideshow: centre click pauses / resumes. Edges stay navigation above.
    // Ignore the second press of a double-click so we do not toggle twice.
    if ((m_view->hostSlideshow().hud().isProgressActive() || m_view->hostSlideshow().hud().isPausedHud())
        && zone == ImageView::EdgeZone::None) {
        if (m_view->hostSlideshow().lastCenterClick().isValid()
            && m_view->hostSlideshow().lastCenterClick().elapsed()
                < QApplication::doubleClickInterval()) {
            event->accept();
            return true;
        }
        m_view->hostSlideshow().lastCenterClick().start();
        emit m_view->slideshowTogglePauseRequested();
        event->accept();
        return true;
    }
    return false;
}

