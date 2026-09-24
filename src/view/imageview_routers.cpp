// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// ImageView thin routers co-located with view/ ownership.

#include "imageview.h"
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>

// --- from src/imageview_paint.cpp ---
void ImageView::paintEvent(QPaintEvent *event)
{
    // All overlays are drawn in drawForeground (single GL-safe paint path).
    // Perf timing lives on HudChrome (BILTOO_PERF / THUMTOO_DEBUG).
    m_hud.runTimedPaint([this, event]() {
        QGraphicsView::paintEvent(event);
    });
}

// --- from src/imageview_paint_background.cpp ---
void ImageView::paintCanvasBackground(QPainter *painter, const QRectF &rect,
                                      qreal viewScale)
{
    m_shell.paintCanvasBackground(painter, rect, viewScale);
}

void ImageView::drawBackground(QPainter *painter, const QRectF &rect)
{
    GUI_BUDGET("ImageView::drawBackground");
    m_shell.paintBackground(painter, rect, transform().m11());
}

void ImageView::drawForeground(QPainter *painter, const QRectF &rect)
{
    GUI_BUDGET("ImageView::drawForeground");
    m_shell.paintForeground(painter, rect);
}

// --- Background settings: ViewShellChrome owns material mutators ---

void ImageView::setBackgroundColor(const QColor &color)
{
    m_shell.setBackgroundColor(color);
}

void ImageView::setBackgroundColorAlt(const QColor &color)
{
    m_shell.setBackgroundColorAlt(color);
}

void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    m_shell.setBackgroundPattern(pattern);
}

void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    m_shell.setCheckerboardWorkspaceOnly(on);
}

void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    m_shell.setWorkspaceBackground(bg);
}

void ImageView::clearWorkspaceBackground()
{
    WorkspaceBackground def;
    setWorkspaceBackground(def);
}

void ImageView::setWorkspaceBackgroundShowDefault(bool on)
{
    m_shell.setWorkspaceBackgroundShowDefault(on);
}

void ImageView::setViewBackground(const WorkspaceBackground &bg)
{
    m_shell.setViewBackground(bg);
}

// --- from src/imageview_input.cpp ---
void ImageView::updateMouseInfo(const QPoint &viewPos)
{
    m_shell.updateMouseInfo(viewPos);
}

void ImageView::wheelEvent(QWheelEvent *event)
{
    m_shell.handleWheel(event);
}

void ImageView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    m_shell.handleResize();
}

bool ImageView::setHoverEdge(EdgeZone zone)
{
    return m_image.setHoverEdge(edgeZoneToPolicy(zone));
}

// --- from src/imageview_input_events.cpp ---
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

