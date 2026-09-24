// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "util/biltoo_thread.h"
#include "text/textsearchpolicy.h"
#include "view/canvaspatterngeometry.h"
#include "view/viewtransform.h"
#include "hud/hudgeometry.h"
#include "slideshow/slideshowclocks.h"
#include "text/textlayergeometry.h"
#include "workspace/pageguidegeometry.h"
#include "image/edgenavpolicy.h"
#include <QElapsedTimer>
#include <QClipboard>
#include <QGuiApplication>
#include <algorithm>
#include "host/thumtoocache.h"
#include "host/pagepath.h"
#include <QFileInfo>
#include <QDebug>
#include <QPixmap>
#include "imageitem.h"

#include <QPainter>
#include <QRadialGradient>
#include <QPainterPath>
#include <QPaintEvent>
#include <QStyleOptionGraphicsItem>
#include "util/biltoo_logging.h"
#include <QGraphicsItem>
#include "view/canvasbackground.h"

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

// --- Background settings (was imageview_background.cpp) ---

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!m_shell.canvasBg().setColor(color)) {
        return;
    }
    setBackgroundBrush(QBrush(m_shell.canvasBg().primaryColor()));
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!m_shell.canvasBg().setColorAlt(color)) {
        return;
    }
    viewport()->update();
}


void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (!m_shell.canvasBg().setPattern(pattern)) {
        return;
    }
    viewport()->update();
}


void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (!m_shell.canvasBg().setCheckerWorkspaceOnly(on)) {
        return;
    }
    viewport()->update();
}


void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    // No-op when durable fields match: leave temporary "show default" preview
    // alone so the toolbar toggle does not desync from paint.
    if (!m_shell.canvasBg().setWorkspace(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_shell.canvasBg().workspaceTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_shell.canvasBg().setWorkspaceTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::clearWorkspaceBackground()
{
    WorkspaceBackground def;
    setWorkspaceBackground(def);
}


void ImageView::setWorkspaceBackgroundShowDefault(bool on)
{
    if (!m_shell.canvasBg().setWorkspaceShowDefault(on)) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setViewBackground(const WorkspaceBackground &bg)
{
    if (!m_shell.canvasBg().setView(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_shell.canvasBg().viewTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_shell.canvasBg().setViewTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}
