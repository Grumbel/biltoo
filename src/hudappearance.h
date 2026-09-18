// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDAPPEARANCE_H
#define HUDAPPEARANCE_H

#include <QColor>
#include <QtGlobal>

/**
 * Pinned / overlay HUD visual preferences (not the transient flash).
 */
struct HudAppearance {
    bool visible = false;
    int fontPointSize = 11;
    QColor textColor{255, 255, 255};
    QColor panelColor{0, 0, 0, 160};

    void setFontPointSize(int pt)
    {
        fontPointSize = qBound(8, pt, 48);
    }

    int effectiveFontPointSize() const { return qBound(8, fontPointSize, 48); }

    QColor effectivePanelColor() const
    {
        if (!panelColor.isValid() || panelColor.alpha() == 0) {
            return QColor(0, 0, 0, 160);
        }
        return panelColor;
    }

    QColor effectiveTextColor(const QColor &fallback = QColor(240, 240, 240)) const
    {
        return textColor.isValid() ? textColor : fallback;
    }
};

#endif // HUDAPPEARANCE_H
