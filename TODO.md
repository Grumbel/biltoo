# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer that treats the image area as a **workspace** rather than a restricted single-image viewport.

## Done

- [x] Classic Qt6 app: menus, toolbar, theme icons, context menu, fullscreen chrome
- [x] Multi-image session, async thumbnails, slideshow, sort, append, rich CLI
- [x] App icon (SVG), .desktop file, toolbar layout with fullscreen on the right
- [x] Workspace foundation (ImageItem, multi-item view, Ctrl+drop, side-by-side)
- [x] Free continuous rotation (Shift + drag around item centre)
- [x] Opacity up/down/reset for overlap comparison
- [x] Raise / Lower z-order
- [x] Double-click thumbnail → add image onto workspace
- [x] Clear workspace extras (keep primary)

- [x] Esc exits fullscreen; toolbar toggle is Ctrl+T (not Tab)
- [x] Natural/locale-aware filename sort (QCollator numeric mode)
- [x] Preferences dialog (slideshow interval, default sort)
- [x] Metadata side panel (Qt text keys + file info; Ctrl+E)
- [x] Workspace mode optional (off by default); DnD adds when enabled
- [x] Workspace canvas persists while mode is off; classic view centres session image
- [x] Undo/redo for workspace transforms; left Select/Pan tool strip
- [x] Workspace thumbnail bar: click toggles image on/off the canvas
- [x] Per-image position/scale/rotation remembered while deselected
- [x] On-canvas scale (corner) and rotate handles on selected items

## Near-term

- [ ] Snap / align helpers
- [ ] Richer Exif via libexiv2 (current panel uses Qt plugin text keys)
- [ ] Rotation angle readout while Shift-dragging (status already updates)
- [ ] Raise / lower and opacity controls directly on the selection chrome

## Later

- [ ] OpenGL path for large images / many items
- [ ] ImageMagick / libvips for broader formats
- [ ] Persist workspace item state across sessions

## Interaction summary

| Gesture | Effect |
|---------|--------|
| Drop (classic mode) | Replace session |
| Shift+Drop (classic) | Append to session |
| Drop (workspace mode) | Add images onto the workspace |
| Click thumbnail (workspace) | Toggle image on/off the workspace (state kept) |
| Ctrl/Shift+click thumbnail (classic) | Enable workspace mode and add image |
| Click item | Select (shows scale/rotate handles) |
| Drag item | Move |
| Drag corner handle | Uniform scale about centre |
| Drag rotate handle | Free rotate about centre |
| Shift+Drag on item | Free rotate (legacy) |
| Wheel | Zoom under cursor |
| Alt+LMB / Middle | Pan view |
| Delete | Remove selected from workspace (state kept) |
| Ctrl+Y | Side-by-side layout |
| Ctrl+Shift+W | Clear workspace extras |
| Ctrl+Shift+Up/Down | Raise / Lower |
| Ctrl+= / Ctrl+- | Opacity up / down |
| Esc | Exit fullscreen |
| Ctrl+T | Toggle toolbar |
| View → Show Scrollbars | Toggle scrollbars |
| F11 | Toggle fullscreen |
| Ctrl+E | Toggle metadata panel |
