// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ITEMHANDLE_H
#define ITEMHANDLE_H

/**
 * Workspace item transform chrome handles (scale / shear / rotate / flips).
 * Shared by ImageItem, ItemHandlePolicy, HandlePressScratch, and view hit paths.
 * Parallel to CropHandle for crop-mode chrome.
 */
enum class ItemHandle {
    None,
    ScaleTopLeft,
    ScaleTopRight,
    ScaleBottomLeft,
    ScaleBottomRight,
    /** Edge stretch (non-uniform): change only one axis. */
    ScaleTop,
    ScaleRight,
    ScaleBottom,
    ScaleLeft,
    /** Shear along local X (parallelogram); H(k) in R·H·S. */
    ShearTop,
    ShearBottom,
    ShearLeft,
    ShearRight,
    RotateTop,
    RotateRight,
    RotateBottom,
    RotateLeft,
    FlipH,
    FlipV,
    /** 90° object rotation (Workspace chrome). */
    Rotate90CCW,
    Rotate90CW,
    Raise,
    Lower,
    ResetScale,
    ResetRotation,
    ResetShear,
    OpacitySlider
};

#endif // ITEMHANDLE_H
