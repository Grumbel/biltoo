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

    const QColor &primaryColor() const { return color; }

    const QColor &altColor() const { return colorAlt; }

    bool isWorkspaceShowDefault() const { return workspaceShowDefault; }

    bool hasWorkspaceTile() const { return !workspaceTile.isNull(); }

    const QPixmap &workspaceTilePixmap() const { return workspaceTile; }

    bool workspaceTilePathMatches(const QString &path) const
    {
        return workspaceTilePath == path;
    }

    void setWorkspaceTile(const QPixmap &px, const QString &path)
    {
        workspaceTile = px;
        workspaceTilePath = path;
    }

    /** @return true when the primary canvas colour changed. */
    bool setColor(const QColor &c)
    {
        if (!c.isValid() || c == color) {
            return false;
        }
        color = c;
        return true;
    }

    bool setColorAlt(const QColor &c)
    {
        if (!c.isValid() || c == colorAlt) {
            return false;
        }
        colorAlt = c;
        return true;
    }

    /** @return true when the pattern changed. */
    bool setPattern(BackgroundPattern p)
    {
        if (pattern == p) {
            return false;
        }
        pattern = p;
        return true;
    }

    bool setCheckerWorkspaceOnly(bool on)
    {
        if (checkerWorkspaceOnly == on) {
            return false;
        }
        checkerWorkspaceOnly = on;
        return true;
    }

    bool setWorkspaceShowDefault(bool on)
    {
        if (workspaceShowDefault == on) {
            return false;
        }
        workspaceShowDefault = on;
        return true;
    }

    /**
     * Install a durable workspace background override.
     * Clears temporary "show default" and drops tile pixmap when path/mode change.
     * @return false when @p bg matches the current override (no-op).
     */
    bool setWorkspace(const WorkspaceBackground &bg)
    {
        if (workspace.matches(bg)) {
            return false;
        }
        workspaceShowDefault = false;
        const bool tilePathChanged =
            bg.mode != WorkspaceBackgroundMode::ImageTile
            || bg.imagePath != workspaceTilePath;
        workspace = bg;
        if (tilePathChanged) {
            clearWorkspaceTile();
        }
        return true;
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
