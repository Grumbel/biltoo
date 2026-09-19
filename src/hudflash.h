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
    /** Identity badge pulse duration after session cursor moves (ms). */
    static constexpr int kIdentityPulseMs = 1000;
    /** Default action flash duration when callers omit an explicit ms (ms). */
    static constexpr int kActionFlashMs = 1400;

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

    void show(const QString &act, const QString &det = QString())
    {
        action = act;
        detail = det;
        visible = true;
        identityPulse = true;
    }

    void setIdentityPulse(bool on) { identityPulse = on; }

    void setPausedLabel(const QString &label)
    {
        action = label;
        detail.clear();
        visible = false;
    }

    void clearAction()
    {
        action.clear();
        detail.clear();
    }
};

#endif // HUDFLASH_H
