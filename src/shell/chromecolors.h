// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CHROMECOLORS_H
#define CHROMECOLORS_H

#include <QColor>

/**
 * Semantic chrome colours for cursor / selection / activity
 * (docs/VIEW_AND_SELECTION.md §4).
 * Theme-independent roles; map to QPalette later if needed.
 */
namespace ChromeColors {

/** Page selection and text selection (Select role). */
inline QColor selectFill(int alpha = 90)
{
    return QColor(40, 140, 255, alpha);
}
inline QColor selectStroke(int alpha = 230)
{
    return QColor(20, 90, 200, alpha);
}

/** Session cursor, spread member marks, viewport indicator (View role). */
inline QColor viewStroke(int alpha = 240)
{
    return QColor(230, 170, 30, alpha);
}
inline QColor viewFill(int alpha = 70)
{
    return QColor(255, 200, 60, alpha);
}

/** Speech / background activity (Activity role). */
inline QColor activityFill(int alpha = 120)
{
    return QColor(40, 200, 100, alpha);
}
inline QColor activityStroke(int alpha = 220)
{
    return QColor(20, 160, 80, alpha);
}

/** Search hits (optional fourth role). */
inline QColor searchStroke(int alpha = 220)
{
    return QColor(180, 80, 220, alpha);
}
inline QColor searchFill(int alpha = 100)
{
    return QColor(180, 80, 220, alpha);
}

} // namespace ChromeColors

#endif // CHROMECOLORS_H
