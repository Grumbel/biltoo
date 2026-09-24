// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Multi-mode viewport pan (owned by ViewShellChrome).

#include "view/viewshellchrome.h"
#include "imageview.h"
#include "image/toolpolicy.h"
#include "display/displaypipelinecontroller.h"

#include <QMouseEvent>
#include <QScrollBar>

bool ViewShellChrome::tryMousePressPan(QMouseEvent *event)
{
    if (!m_view || !event) {
        return false;
    }
    // Middle-button pan in any mode; Gallery also allows Alt+left pan.
    if (!m_view->hostSlideshow().dwell().isMotionActive()
        && (event->button() == Qt::MiddleButton
            || (event->button() == Qt::LeftButton
                && ((m_view->isImageMode() && m_viewport.isImageModeLeftDragPan())
                    || (m_view->isWorkspaceMode()
                        && m_view->currentTool() == Tool::Pan)
                    || (m_view->isGalleryMode()
                        && (event->modifiers() & Qt::AltModifier))
                    || (event->modifiers() & Qt::AltModifier))))) {
        if (!(m_view->isWorkspaceMode() && (event->modifiers() & Qt::ShiftModifier)
              && event->button() == Qt::LeftButton)) {
            m_viewport.beginPan(event->pos());
            m_view->setCursor(Qt::ClosedHandCursor);
            event->accept();
            return true;
        }
    }
    if (event->button() == Qt::MiddleButton
        && !m_view->hostSlideshow().dwell().isMotionActive()) {
        m_viewport.beginPan(event->pos());
        m_view->setCursor(Qt::ClosedHandCursor);
        event->accept();
        return true;
    }
    return false;
}

bool ViewShellChrome::tryMouseMovePan(QMouseEvent *event)
{
    if (!m_view || !event || !m_viewport.isPanning()) {
        return false;
    }
    // Ken Burns / pure-clock motion owns the viewport — do not fight it.
    if (m_view->hostSlideshow().dwell().isMotionActive()) {
        m_viewport.endPan();
        event->accept();
        return true;
    }
    const QPoint delta = m_viewport.panDeltaFrom(event->pos());
    m_viewport.updatePanPos(event->pos());
    // Grow the free-form sceneRect with the view so middle-drag is never
    // clamped against a stale zero-range scrollbar.
    if (m_view->isWorkspaceMode()) {
        m_view->updateWorkspaceSceneRect();
    }
    if (QScrollBar *h = m_view->horizontalScrollBar()) {
        h->setValue(h->value() - delta.x());
    }
    if (QScrollBar *v = m_view->verticalScrollBar()) {
        v->setValue(v->value() - delta.y());
    }
    // Tile LOD timer stops once the viewport is covered. Panning changes the
    // visible set without a zoom/climb event — coalesce issues (not per move).
    m_view->hostDisplayPipeline().scheduleTileLodAfterInteraction(32);
    event->accept();
    return true;
}

bool ViewShellChrome::tryMouseReleasePan(QMouseEvent *event)
{
    if (!m_view || !event || !m_viewport.isPanning()
        || (event->button() != Qt::MiddleButton && event->button() != Qt::LeftButton)) {
        return false;
    }
    m_viewport.endPan();
    m_view->restoreToolCursor();
    m_view->hostDisplayPipeline().tickPrimaryTileLod(8);
    event->accept();
    return true;
}
