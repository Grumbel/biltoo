// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TOOLCURSORS_H
#define TOOLCURSORS_H

#include "annotation/annotationtypes.h"
#include "imageview_types.h"

#include <QCursor>

/**
 * Custom pixmap cursors for canvas / annotation / crop / attention tools.
 *
 * SVGs live under :/icons/cursors/. Each precision tool pairs a clear
 * crosshair (hotspot at centre) with a small coloured tool badge.
 * Resize-handle shapes stay as stock Qt::CursorShape (system-themed).
 *
 * Colour ARGB cursors work on X11 (XRender), Wayland, and Windows via
 * QCursor(QPixmap). If a resource fails to load, falls back to the stock
 * shape that matched the previous ToolPolicy behaviour.
 */
namespace ToolCursors {

/** Canvas Tool (Select / Pan / Zoom). */
QCursor forTool(Tool tool);

/** Open-hand idle vs closed-hand while panning. */
QCursor panOpen();
QCursor panClosed();

/** Annotation tools; None restores the canvas tool via the caller. */
QCursor forAnnotation(Annotation::Tool tool);

/** Crop mode: outside the crop rect (handles keep stock size cursors). */
QCursor cropOutside();

/** Attention region drawing (handles keep SizeAll). */
QCursor attentionDraw();

/** Text-layer region pick / select. */
QCursor textSelect();

/** Generic precision crosshair (zoom arm, workspace draw, etc.). */
QCursor crosshair();

} // namespace ToolCursors

#endif // TOOLCURSORS_H
