// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Z-key / Workspace Zoom-tool rubber-band (owned by ImageController).

#include "image/imagecontroller.h"
#include "imageview.h"
#include "image/toolpolicy.h"
#include "view/viewframing.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QWidget>

void ImageController::cancelZoomRegion()
{
    m_zoomRegion.disarm();
    m_zoomRegion.hideRubber();
    if (!m_view->hostChrome().isPanning()
        && !m_view->hostWorkspace().itemInteract().isRotating()) {
        m_view->setCursor(ToolPolicy::cursorFor(m_view->hostWorkspace().currentTool()));
    }
    emit m_view->statusChanged();
}

void ImageController::armZoomRegion()
{
    if (m_view->liveItems().isEmpty() && m_view->isImageMode() && !hasClassicPath()) {
        return;
    }
    cancelZoomRegion();
    m_zoomRegion.arm();
    m_view->setCursor(Qt::CrossCursor);
    emit m_view->statusChanged();
    if (QWidget *vp = m_view->viewport()) {
        vp->update();
    }
}

bool ImageController::tryMousePressZoomRegion(QMouseEvent *event)
{
    if (!(m_zoomRegion.isArmed()
          || (m_view->isWorkspaceMode() && m_view->hostWorkspace().currentTool() == Tool::Zoom))
        || event->button() != Qt::LeftButton) {
        return false;
    }
    m_zoomRegion.tryBeginPress(event->pos(), m_view->viewport());
    event->accept();
    return true;
}

bool ImageController::tryMouseMoveZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.tryUpdateMove(event->pos())) {
        return false;
    }
    event->accept();
    return true;
}

bool ImageController::tryMouseReleaseZoomRegion(QMouseEvent *event)
{
    if (!m_zoomRegion.isDragging()) {
        return false;
    }
    QRect viewRect;
    if (m_zoomRegion.tryEndRelease(event->pos(), &viewRect)) {
        const QRectF sceneRect = m_view->mapToScene(viewRect).boundingRect();
        if (sceneRect.isValid() && !sceneRect.isEmpty()) {
            releaseStickyZoom();
            m_framing.clearFitFill();
            m_view->fitInView(sceneRect, Qt::KeepAspectRatio);
            emit m_view->statusChanged();
        }
    }
    event->accept();
    return true;
}

bool ImageController::tryKeyPressZoomRegion(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape || !m_zoomRegion.isActive()) {
        return false;
    }
    cancelZoomRegion();
    event->accept();
    return true;
}
