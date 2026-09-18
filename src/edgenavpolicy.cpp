// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "edgenavpolicy.h"

#include <QtGlobal>

namespace EdgeNavPolicy {

int zoneWidth(int viewportWidth)
{
    return qMax(48, static_cast<int>(viewportWidth * 0.12));
}

int zoneHeight(int viewportHeight)
{
    return qMax(40, static_cast<int>(viewportHeight * 0.10));
}

Zone zoneAt(const QPoint &viewPos, int viewportWidth, int viewportHeight,
            bool galleryReturnAvailable, bool imageModeNavEnabled)
{
    // Top strip: back to Gallery or Workspace (when Image was opened from there).
    // Takes priority over left/right so the upper corners still return.
    if (galleryReturnAvailable && viewPos.y() < zoneHeight(viewportHeight)) {
        return Zone::GalleryReturn;
    }
    if (!imageModeNavEnabled) {
        return Zone::None;
    }
    const int zone = zoneWidth(viewportWidth);
    if (viewPos.x() < zone) {
        return Zone::Previous;
    }
    if (viewPos.x() > viewportWidth - zone) {
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
    case Zone::GalleryReturn:
        layout.fillRect = QRect(0, 0, w, zoneH);
        layout.buttonCenter = QPoint(w / 2, margin + r);
        break;
    case Zone::Previous:
        layout.fillRect = QRect(0, 0, zoneW, h);
        layout.buttonCenter = QPoint(margin + r, h / 2);
        break;
    case Zone::Next:
        layout.fillRect = QRect(w - zoneW, 0, zoneW, h);
        layout.buttonCenter = QPoint(w - margin - r, h / 2);
        break;
    case Zone::None:
    default:
        break;
    }
    return layout;
}

} // namespace EdgeNavPolicy
