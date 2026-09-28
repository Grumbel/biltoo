// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOOLPOLICY_H
#define TOOLPOLICY_H

#include "image/toolcursors.h"
#include "imageview_types.h"

#include <QCursor>
#include <Qt>
#include <QtCore/qnamespace.h>

/**
 * Pure Workspace / Image tool chrome: cursor and rubber-band drag mode.
 * ImageView still owns Tool state and applies Qt widgets.
 */
namespace ToolPolicy {

/** Cursor for @p tool (custom pixmap with stock fallback). */
inline QCursor cursorFor(Tool tool)
{
    return ToolCursors::forTool(tool);
}

/**
 * Workspace drag mode: Select enables rubber-band multi-select; Pan/Zoom do not.
 * Gallery always uses RubberBandDrag from its own path.
 */
inline bool workspaceRubberBand(Tool tool)
{
    return tool == Tool::Select;
}

} // namespace ToolPolicy

#endif // TOOLPOLICY_H
