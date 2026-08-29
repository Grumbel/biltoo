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

## Near-term

- [ ] Snap / align helpers
- [ ] Richer Exif via libexiv2 (current panel uses Qt plugin text keys)
- [ ] Rotation angle readout while Shift-dragging (status already updates)

## Later

- [ ] OpenGL path for large images / many items
- [ ] ImageMagick / libvips for broader formats
- [ ] Per-image view state persistence

## Interaction summary

| Gesture | Effect |
|---------|--------|
| Drop | Replace session |
| Shift+Drop | Append to session |
| Ctrl+Drop | Add onto workspace |
| Double-click thumbnail | Add that image onto workspace |
| Click item | Select |
| Drag item | Move |
| Shift+Drag on item | Free rotate |
| Wheel | Zoom under cursor |
| Alt+LMB / Middle | Pan view |
| Delete | Remove selected from workspace |
| Ctrl+Y | Side-by-side layout |
| Ctrl+Shift+W | Clear workspace extras |
| Ctrl+Shift+Up/Down | Raise / Lower |
| Ctrl+= / Ctrl+- | Opacity up / down |
| Esc | Exit fullscreen |
| Ctrl+T | Toggle toolbar |
| F11 | Toggle fullscreen |
| Ctrl+E | Toggle metadata panel |
