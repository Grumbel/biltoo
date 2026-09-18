// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDAPPEARANCE_H
#define HUDAPPEARANCE_H

#include <QColor>

/**
 * Pinned / overlay HUD visual preferences (not the transient flash).
 */
struct HudAppearance {
    bool visible = false;
    int fontPointSize = 11;
    QColor textColor{255, 255, 255};
    QColor panelColor{0, 0, 0, 160};
};

#endif // HUDAPPEARANCE_H
