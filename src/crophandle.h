// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CROPHANDLE_H
#define CROPHANDLE_H

/**
 * Crop-mode draft interaction handles (viewport chrome).
 * Shared by CropSession, CropGeometry, and ImageView input/paint.
 */
enum class CropHandle {
    None,
    /** Translate the draft rect without changing size. */
    Move,
    /** Rotate the draft about its centre. */
    Rotate,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    /**
     * Toggle: when on, the draft may extend outside the image (pad on apply).
     * When off, the draft is clamped to the image bounds.
     */
    ExpandToggle,
    /**
     * Shrink the draft to content: median border colour as background,
     * trim empty margins (GIMP-style autocrop). Axis-aligned.
     */
    Auto,
    /** Clears the draft rect to the full image (reset session crop on apply). */
    Reset,
    /** Leave crop mode and discard the draft. */
    Cancel,
    /** Leave crop mode and commit the draft (same as Enter). */
    Close
};

#endif // CROPHANDLE_H
