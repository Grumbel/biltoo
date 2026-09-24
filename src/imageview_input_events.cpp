// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// QGraphicsView input overrides: thin routers to ViewShellChrome.
// Transform undo: item/geometryundocommand.cpp.

#include "imageview.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>

void ImageView::mousePressEvent(QMouseEvent *event)
{
    if (m_shell.handleMousePress(event)) {
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseMove(event)) {
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageView::restoreToolCursor()
{
    m_shell.restoreToolCursor();
}

void ImageView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseRelease(event)) {
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageView::keyPressEvent(QKeyEvent *event)
{
    if (m_shell.handleKeyPress(event)) {
        return;
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_shell.handleMouseDoubleClick(event)) {
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageView::leaveEvent(QEvent *event)
{
    m_shell.handleLeave();
    QGraphicsView::leaveEvent(event);
}

bool ImageView::viewportEvent(QEvent *event)
{
    if (m_shell.handleViewportEvent(event)) {
        return event->isAccepted();
    }
    return QGraphicsView::viewportEvent(event);
}

void ImageView::dragEnterEvent(QDragEnterEvent *event)
{
    m_shell.dragEnterEvent(event);
}

void ImageView::dragMoveEvent(QDragMoveEvent *event)
{
    m_shell.dragMoveEvent(event);
}

void ImageView::dropEvent(QDropEvent *event)
{
    m_shell.dropEvent(event);
}
