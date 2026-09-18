// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDGEOMETRY_H
#define HUDGEOMETRY_H

#include <QRect>
#include <QtGlobal>

/**
 * Pure HUD panel placement (viewport CSS pixels). Text measurement stays
 * with the caller (font metrics); this only sizes and positions the box.
 */
namespace HudGeometry {

struct PanelBox {
    int x = 0;
    int y = 0;
    int bgW = 0;
    int bgH = 0;
};

/**
 * Place a panel given measured text size.
 * @p fromRight / @p fromBottom / @p centre select anchor rules; result is
 * clamped fully on-screen with @p margin inset.
 */
inline PanelBox placePanel(int viewW, int viewH, int textW, int textH,
                           int margin, int pad, int anchorX, int anchorY,
                           bool fromRight, bool fromBottom, bool centre)
{
    const int maxBgW = qMax(40, viewW - 2 * margin);
    const int maxTextW = qMax(20, maxBgW - 2 * pad);
    textW = qMin(textW, maxTextW);
    PanelBox box;
    box.bgW = qMin(maxBgW, textW + 2 * pad);
    box.bgH = textH + 2 * pad;
    if (centre) {
        box.x = (viewW - box.bgW) / 2;
        box.y = (viewH - box.bgH) / 2;
    } else {
        box.x = fromRight ? (viewW - margin - box.bgW) : anchorX;
        box.y = fromBottom ? (viewH - margin - box.bgH) : anchorY;
    }
    box.x = qBound(margin, box.x, viewW - margin - box.bgW);
    box.y = qBound(margin, box.y, viewH - margin - box.bgH);
    return box;
}

inline QRect panelRect(const PanelBox &box)
{
    return QRect(box.x, box.y, box.bgW, box.bgH);
}

} // namespace HudGeometry

#endif // HUDGEOMETRY_H
