// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// Image-mode edge hover zones (prev/next/gallery return).

#include "imageview.h"
#include "edgenavpolicy.h"
#include "toolpolicy.h"

int ImageView::edgeZoneWidth() const
{
    return EdgeNavPolicy::zoneWidth(width());
}

int ImageView::edgeZoneHeight() const
{
    return EdgeNavPolicy::zoneHeight(height());
}

bool ImageView::setHoverEdge(EdgeZone zone)
{
    if (zone == m_hoverEdge) {
        return false;
    }
    m_hoverEdge = zone;
    if (isNavEdge(m_hoverEdge)) {
        setCursor(Qt::PointingHandCursor);
    } else if (!m_chrome.isPanning() && !m_itemInteract.isRotating()) {
        setCursor(ToolPolicy::cursorFor(m_tool));
    }
    if (viewport()) {
        viewport()->update();
    }
    return true;
}

void ImageView::updateHoverEdge(const QPoint &viewPos)
{
    (void)setHoverEdge(edgeZoneAt(viewPos));
}
