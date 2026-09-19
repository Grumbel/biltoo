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

    /** Preferences / effective HUD text size (8–48 pt). */
    static int clampFontPointSize(int pt) { return qBound(8, pt, 48); }

    void setFontPointSize(int pt)
    {
        fontPointSize = clampFontPointSize(pt);
    }

    int effectiveFontPointSize() const { return clampFontPointSize(fontPointSize); }

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

    bool setVisible(bool on)
    {
        if (visible == on) {
            return false;
        }
        visible = on;
        return true;
    }

    bool setTextColor(const QColor &c)
    {
        if (!c.isValid() || c == textColor) {
            return false;
        }
        textColor = c;
        return true;
    }

    bool setPanelColor(const QColor &c)
    {
        if (!c.isValid() || c == panelColor) {
            return false;
        }
        panelColor = c;
        return true;
    }
};

#endif // HUDAPPEARANCE_H
