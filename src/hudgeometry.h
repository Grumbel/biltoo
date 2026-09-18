// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDGEOMETRY_H
#define HUDGEOMETRY_H

#include <QFontMetrics>
#include <QRect>
#include <QStringList>
#include <QtGlobal>

/**
 * Pure HUD panel placement (viewport CSS pixels). Text measurement stays
 * with the caller (font metrics); this only sizes and positions the box.
 */
namespace HudGeometry {

inline int maxPanelBgW(int viewW, int margin)
{
    return qMax(40, viewW - 2 * margin);
}

inline int maxPanelTextW(int maxBgW, int pad)
{
    return qMax(20, maxBgW - 2 * pad);
}

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

/**
 * Split HUD lines on " | " when the full string exceeds @p maxTextW;
 * otherwise elide middle. Pure except for QFontMetrics measurement.
 */
inline QStringList wrapHudLine(const QString &text, const QFontMetrics &metrics,
                               int maxTextW)
{
    QStringList out;
    if (text.isEmpty()) {
        return out;
    }
    if (metrics.horizontalAdvance(text) <= maxTextW) {
        out << text;
        return out;
    }
    const QString sep = QStringLiteral(" | ");
    const QStringList parts = text.split(sep, Qt::KeepEmptyParts);
    if (parts.size() <= 1) {
        out << metrics.elidedText(text, Qt::ElideMiddle, maxTextW);
        return out;
    }
    QString current;
    for (const QString &part : parts) {
        const QString candidate = current.isEmpty() ? part : current + sep + part;
        if (metrics.horizontalAdvance(candidate) <= maxTextW) {
            current = candidate;
            continue;
        }
        if (!current.isEmpty()) {
            out << current;
        }
        if (metrics.horizontalAdvance(part) <= maxTextW) {
            current = part;
        } else {
            out << metrics.elidedText(part, Qt::ElideMiddle, maxTextW);
            current.clear();
        }
    }
    if (!current.isEmpty()) {
        out << current;
    }
    return out;
}

inline int clampTitlePointSize(int basePt)
{
    return qBound(12, basePt + 4, 28);
}

inline int clampHintPointSize(int basePt)
{
    return qBound(10, basePt + 1, 20);
}

inline int clampEdgePointSize(int basePt)
{
    return qBound(9, basePt, 16);
}

} // namespace HudGeometry

#endif // HUDGEOMETRY_H
