// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HUDFLASH_H
#define HUDFLASH_H

#include <QString>

/**
 * Brief top-left HUD action flash (fit mode, slideshow step, …).
 * Flash QTimer stays on ImageView.
 */
struct HudFlash {
    bool visible = false;
    bool identityPulse = false;
    QString action;
    QString detail;

    void clear()
    {
        visible = false;
        identityPulse = false;
        action.clear();
        detail.clear();
    }
};

#endif // HUDFLASH_H
