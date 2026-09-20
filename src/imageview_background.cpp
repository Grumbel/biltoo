// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// View and Workspace background color/pattern settings.

#include "imageview.h"

void ImageView::setBackgroundColor(const QColor &color)
{
    if (!m_canvasBg.setColor(color)) {
        return;
    }
    setBackgroundBrush(QBrush(m_canvasBg.primaryColor()));
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setBackgroundColorAlt(const QColor &color)
{
    if (!m_canvasBg.setColorAlt(color)) {
        return;
    }
    viewport()->update();
}


void ImageView::setBackgroundPattern(BackgroundPattern pattern)
{
    if (!m_canvasBg.setPattern(pattern)) {
        return;
    }
    viewport()->update();
}


void ImageView::setCheckerboardWorkspaceOnly(bool on)
{
    if (!m_canvasBg.setCheckerWorkspaceOnly(on)) {
        return;
    }
    viewport()->update();
}


void ImageView::setWorkspaceBackground(const WorkspaceBackground &bg)
{
    // No-op when durable fields match: leave temporary "show default" preview
    // alone so the toolbar toggle does not desync from paint.
    if (!m_canvasBg.setWorkspace(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_canvasBg.workspaceTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_canvasBg.setWorkspaceTile(px, bg.imagePath);
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
    if (!m_canvasBg.setWorkspaceShowDefault(on)) {
        return;
    }
    if (viewport()) {
        viewport()->update();
    }
}


void ImageView::setViewBackground(const WorkspaceBackground &bg)
{
    if (!m_canvasBg.setView(bg)) {
        return;
    }
    if (bg.mode == WorkspaceBackgroundMode::ImageTile && !bg.imagePath.isEmpty()) {
        if (!m_canvasBg.viewTilePathMatches(bg.imagePath)) {
            QPixmap px(bg.imagePath);
            if (!px.isNull()) {
                m_canvasBg.setViewTile(px, bg.imagePath);
            }
        }
    }
    if (viewport()) {
        viewport()->update();
    }
}



