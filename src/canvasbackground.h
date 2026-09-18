// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CANVASBACKGROUND_H
#define CANVASBACKGROUND_H

#include "imageview_types.h"

#include <QColor>
#include <QPixmap>
#include <QString>

enum class BackgroundPattern {
    Solid,
    Checkerboard
};

/**
 * Viewport / workspace background colours, pattern, and optional image tile.
 */
struct CanvasBackground {
    QColor color{42, 42, 42};
    QColor colorAlt{48, 48, 48};
    BackgroundPattern pattern = BackgroundPattern::Checkerboard;
    bool checkerWorkspaceOnly = true;
    WorkspaceBackground workspace;
    /** When true, paint AppDefault even if workspace is custom. */
    bool workspaceShowDefault = false;
    QPixmap workspaceTile;
    QString workspaceTilePath;

    void clearWorkspaceTile()
    {
        workspaceTile = {};
        workspaceTilePath.clear();
    }

    /** App-default checker when pattern is Checkerboard (optional WS-only). */
    bool useChecker(bool isWorkspaceMode) const
    {
        return pattern == BackgroundPattern::Checkerboard
            && (!checkerWorkspaceOnly || isWorkspaceMode);
    }

    QColor checkerAlt() const
    {
        return colorAlt.isValid() ? colorAlt : color.lighter(120);
    }
};

#endif // CANVASBACKGROUND_H
