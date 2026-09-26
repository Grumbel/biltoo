// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VIEWINTERACTION_H
#define VIEWINTERACTION_H

#include "imageview_types.h"

/**
 * Shared canvas interaction state (tool) for Image / Gallery / Workspace.
 *
 * Presentation modes interpret the tool differently (see ToolPolicy and
 * MODE_OWNERSHIP); the active tool itself is one radio across modes.
 */
class ViewInteraction
{
public:
    Tool tool() const { return m_tool; }

    /** @return true if the tool changed. */
    bool setTool(Tool tool)
    {
        if (m_tool == tool) {
            return false;
        }
        m_tool = tool;
        return true;
    }

    /** Recommended default when entering a presentation mode. */
    static Tool defaultToolForMode(int viewMode)
    {
        // ImageView::ViewMode: Image=0, Gallery=1, Workspace=2
        if (viewMode == 0) {
            return Tool::Pan;
        }
        return Tool::Select;
    }

private:
    Tool m_tool = Tool::Select;
};

#endif // VIEWINTERACTION_H
