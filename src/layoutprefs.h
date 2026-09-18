// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTPREFS_H
#define LAYOUTPREFS_H

#include "imageview_types.h"

/**
 * Workspace layout mode and grid/masonry column counts.
 */
struct LayoutPrefs {
    LayoutMode mode = LayoutMode::FreeForm;
    int masonryColumns = 3;
    int gridColumns = 0;
    int masonryRows = 3;
};

#endif // LAYOUTPREFS_H
