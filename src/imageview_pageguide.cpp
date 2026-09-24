// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Page-guide public API + print routing (Workspace / Image own paint bodies).

#include "imageview.h"

#include <QPainter>
#include <QGraphicsScene>

void ImageView::setPageGuideVisible(bool on)
{
    m_workspace.setPageGuideVisible(on);
}

void ImageView::setPageGuideFromPrinter(const QPrinter &printer)
{
    m_workspace.setPageGuideFromPrinter(printer);
}

QRectF ImageView::pageGuideSceneRect() const
{
    return m_workspace.pageGuideSceneRect();
}

void ImageView::fitPageGuideToContent(qreal marginPx)
{
    m_workspace.fitPageGuideToContent(marginPx);
}

void ImageView::setPageGuideSelected(bool on)
{
    m_workspace.setPageGuideSelected(on);
}

void ImageView::renderForPrint(QPainter *painter, const QRectF &pageRect) const
{
    if (!painter || !painter->isActive() || !pageRect.isValid()) {
        return;
    }

    if (isWorkspaceMode()) {
        m_workspace.renderForPrint(painter, pageRect);
        return;
    }

    if (isImageMode()) {
        m_image.renderForPrint(painter, pageRect);
        return;
    }

    if (m_scene) {
        QRectF source = m_scene->itemsBoundingRect();
        if (!source.isValid() || source.isEmpty()) {
            return;
        }
        source.adjust(-4, -4, 4, 4);
        m_scene->render(painter, pageRect, source, Qt::KeepAspectRatio);
    }
}
