// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CHROMECOLORS_H
#define CHROMECOLORS_H

#include <QColor>

/**
 * Semantic chrome colours for cursor / selection / activity / search
 * (docs/VIEW_AND_SELECTION.md §4).
 *
 * Base RGB values are configurable (Preferences → Interface → Chrome).
 * Call sites pass alpha; fill/stroke helpers apply it to the current bases.
 */
namespace ChromeColors {

enum class Role {
    Select,
    View,
    Activity,
    Search,
};

/** Opaque RGB base for fills (alpha ignored). */
QColor fillBase(Role role);
/** Opaque RGB base for strokes (alpha ignored). */
QColor strokeBase(Role role);

void setFillBase(Role role, const QColor &rgb);
void setStrokeBase(Role role, const QColor &rgb);

/** Built-in defaults (brighter cyan Select so it separates from grey chrome). */
QColor defaultFillBase(Role role);
QColor defaultStrokeBase(Role role);

void resetToDefaults();

/** Page selection and text selection (Select role). */
QColor selectFill(int alpha = 90);
QColor selectStroke(int alpha = 230);

/** Session cursor, spread member marks, viewport indicator (View role). */
QColor viewStroke(int alpha = 240);
QColor viewFill(int alpha = 70);

/** Speech / background activity (Activity role). */
QColor activityFill(int alpha = 120);
QColor activityStroke(int alpha = 220);

/** Search hits. */
QColor searchStroke(int alpha = 220);
QColor searchFill(int alpha = 100);

} // namespace ChromeColors

#endif // CHROMECOLORS_H
