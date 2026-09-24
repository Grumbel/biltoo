// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "imageview.h"
#include "image/edgenavpolicy.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QWheelEvent>

ImageView::EdgeZone ImageView::edgeZoneAt(const QPoint &viewPos) const
{
    return edgeZoneFromPolicy(m_image.edgeZoneAt(viewPos));
}

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
