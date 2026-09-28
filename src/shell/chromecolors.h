// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CHROMECOLORS_H
#define CHROMECOLORS_H

#include <QColor>
#include <QGuiApplication>
#include <QPalette>

/**
 * Semantic chrome colours for cursor / selection / activity / search
 * (docs/VIEW_AND_SELECTION.md §4).
 *
 * Roles are theme-aware: hues stay fixed (blue Select, amber View, green
 * Activity, violet Search); lightness is picked for contrast against the
 * application Window palette (light vs dark chrome).
 */
namespace ChromeColors {

/** True when the app Window background is dark (lightness < 0.5). */
inline bool isDarkChrome()
{
    if (!QGuiApplication::instance()) {
        return true;
    }
    return QGuiApplication::palette().color(QPalette::Window).lightnessF() < 0.5;
}

/** Page selection and text selection (Select role). */
inline QColor selectFill(int alpha = 90)
{
    if (isDarkChrome()) {
        return QColor(40, 140, 255, alpha);
    }
    return QColor(20, 100, 210, alpha);
}
inline QColor selectStroke(int alpha = 230)
{
    if (isDarkChrome()) {
        return QColor(20, 90, 200, alpha);
    }
    return QColor(10, 70, 170, alpha);
}

/** Session cursor, spread member marks, viewport indicator (View role). */
inline QColor viewStroke(int alpha = 240)
{
    if (isDarkChrome()) {
        return QColor(230, 170, 30, alpha);
    }
    return QColor(180, 120, 0, alpha);
}
inline QColor viewFill(int alpha = 70)
{
    if (isDarkChrome()) {
        return QColor(255, 200, 60, alpha);
    }
    return QColor(230, 170, 20, alpha);
}

/** Speech / background activity (Activity role). */
inline QColor activityFill(int alpha = 120)
{
    if (isDarkChrome()) {
        return QColor(40, 200, 100, alpha);
    }
    return QColor(10, 150, 70, alpha);
}
inline QColor activityStroke(int alpha = 220)
{
    if (isDarkChrome()) {
        return QColor(20, 160, 80, alpha);
    }
    return QColor(0, 120, 55, alpha);
}

/** Search hits (optional fourth role). */
inline QColor searchStroke(int alpha = 220)
{
    if (isDarkChrome()) {
        return QColor(180, 80, 220, alpha);
    }
    return QColor(140, 40, 180, alpha);
}
inline QColor searchFill(int alpha = 100)
{
    if (isDarkChrome()) {
        return QColor(180, 80, 220, alpha);
    }
    return QColor(150, 50, 190, alpha);
}

} // namespace ChromeColors

#endif // CHROMECOLORS_H
