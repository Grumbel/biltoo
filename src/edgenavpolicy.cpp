// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "edgenavpolicy.h"

#include <QtGlobal>

namespace EdgeNavPolicy {

int zoneWidth(int viewportWidth)
{
    return qMax(kZoneWidthFloor, static_cast<int>(viewportWidth * kZoneWidthFrac));
}

int zoneHeight(int viewportHeight)
{
    return qMax(kZoneHeightFloor, static_cast<int>(viewportHeight * kZoneHeightFrac));
}

int edgeSpanAlong(int edgeLength)
{
    if (edgeLength <= 0) {
        return 0;
    }
    return qMax(1, static_cast<int>(edgeLength * kEdgeSpanFrac));
}

Zone zoneAt(const QPoint &viewPos, int viewportWidth, int viewportHeight,
            bool galleryReturnAvailable, bool imageModeNavEnabled)
{
    // Top band: centred ~80% width — avoids left/right corners and side status.
    if (galleryReturnAvailable) {
        const int zh = zoneHeight(viewportHeight);
        const int spanW = edgeSpanAlong(viewportWidth);
        const int x0 = (viewportWidth - spanW) / 2;
        if (viewPos.y() >= 0 && viewPos.y() < zh
            && viewPos.x() >= x0 && viewPos.x() < x0 + spanW) {
            return Zone::GalleryReturn;
        }
    }
    if (!imageModeNavEnabled) {
        return Zone::None;
    }
    const int zw = zoneWidth(viewportWidth);
    const int spanH = edgeSpanAlong(viewportHeight);
    const int y0 = (viewportHeight - spanH) / 2;
    if (viewPos.y() < y0 || viewPos.y() >= y0 + spanH) {
        return Zone::None;
    }
    if (viewPos.x() < zw) {
        return Zone::Previous;
    }
    if (viewPos.x() > viewportWidth - zw) {
        return Zone::Next;
    }
    return Zone::None;
}

ChromeLayout chromeLayout(Zone zone, const QRect &viewport, int zoneW, int zoneH,
                          int buttonRadius, int margin)
{
    ChromeLayout layout;
    const int r = buttonRadius;
    const int w = viewport.width();
    const int h = viewport.height();
    switch (zone) {
    case Zone::GalleryReturn: {
        const int spanW = edgeSpanAlong(w);
        const int x0 = (w - spanW) / 2;
        layout.fillRect = QRect(x0, 0, spanW, zoneH);
        layout.buttonCenter = QPoint(w / 2, margin + r);
        break;
    }
    case Zone::Previous: {
        const int spanH = edgeSpanAlong(h);
        const int y0 = (h - spanH) / 2;
        layout.fillRect = QRect(0, y0, zoneW, spanH);
        layout.buttonCenter = QPoint(margin + r, h / 2);
        break;
    }
    case Zone::Next: {
        const int spanH = edgeSpanAlong(h);
        const int y0 = (h - spanH) / 2;
        layout.fillRect = QRect(w - zoneW, y0, zoneW, spanH);
        layout.buttonCenter = QPoint(w - margin - r, h / 2);
        break;
    }
    case Zone::None:
    default:
        break;
    }
    return layout;
}

} // namespace EdgeNavPolicy
