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

    /** @return true when the layout mode changed. */
    bool setMode(LayoutMode m)
    {
        if (mode == m) {
            return false;
        }
        mode = m;
        return true;
    }

    /** 0 = automatic for grid/flow. */
    void setGridColumns(int columns) { gridColumns = qMax(0, columns); }

    static constexpr int kMaxMasonryBands = 32;

    void setMasonryColumns(int columns)
    {
        masonryColumns = qBound(1, columns, kMaxMasonryBands);
    }

    void setMasonryRows(int rows)
    {
        masonryRows = qBound(1, rows, kMaxMasonryBands);
    }
};

#endif // LAYOUTPREFS_H
