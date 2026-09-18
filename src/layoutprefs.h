// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LAYOUTPREFS_H
#define LAYOUTPREFS_H

#include "imageview_types.h"

#include <QtGlobal>

/**
 * Workspace layout mode and grid/masonry column counts.
 */
struct LayoutPrefs {
    LayoutMode mode = LayoutMode::FreeForm;
    int masonryColumns = 3;
    int gridColumns = 0;
    int masonryRows = 3;

    /** 0 = automatic for grid/flow. */
    void setGridColumns(int columns) { gridColumns = qMax(0, columns); }

    void setMasonryColumns(int columns) { masonryColumns = qBound(1, columns, 32); }

    void setMasonryRows(int rows) { masonryRows = qBound(1, rows, 32); }
};

#endif // LAYOUTPREFS_H
