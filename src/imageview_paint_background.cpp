// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"

#include <QPainter>

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
