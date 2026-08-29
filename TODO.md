# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

# QImgView TODO

## Core Vision

QImgView is a classic Qt image viewer that treats the image area as a **workspace** rather than a restricted single-image viewport.

- Support individual images **or** groups of images (multiple files given on the command line or dropped).
- Users should be able to drop multiple images onto the view to compare them side-by-side or overlapping.
- Free rotation and continuous scale/zoom (not limited to 90° or fixed steps).
- Toolbar inspired by Ristretto: Load/Open, Zoom In, Zoom Out, 1:1, Fit, Fullscreen, plus Rotate Left / Rotate Right.
- Thumbnail panel at the bottom when multiple images are loaded.
- Optional side panel for Exif / metadata display.
- No image editing in the first versions (pixel changes). We may later persist per-image rotate/zoom settings.

## Done (v0.1 → workspace foundation)

- [x] Classic Qt6 app: menus, toolbar, theme icons, context menu, fullscreen chrome
- [x] Multi-image session, async thumbnails, slideshow, sort, append, rich CLI
- [x] Status bar with mouse pixel / RGB
- [x] QSettings, directory open, recursive
- [x] **Workspace foundation**: ImageItem + multi-item ImageView
  - [x] loadImage() replaces workspace; addImage() places additional items
  - [x] Select / drag to move items; wheel zooms item under cursor
  - [x] Zoom / rotate actions apply to the selection (or sole item)
  - [x] Side-by-side layout action (Ctrl+Y)
  - [x] Ctrl+drop adds images to the workspace for comparison
  - [x] Delete/Backspace removes selected items from the workspace
  - [x] Alt+drag or middle-button pans the view

## Near-term workspace

- [ ] Continuous free rotation (drag handle or modifier+drag), not only 90°
- [ ] Opacity / blend for overlap comparison
- [ ] Snap / align helpers
- [ ] Z-order (raise / lower)
- [ ] Double-click thumbnail to add that image onto the workspace
- [ ] "Clear workspace extras" while keeping the primary session image

## Near-term app

- [ ] Preferences dialog (slideshow interval, default sort, …)
- [ ] Natural/locale-aware filename sort
- [ ] Optional Exif side panel

## Later

- [ ] OpenGL path for large images / many items
- [ ] ImageMagick / libvips for broader formats
- [ ] Per-image view state persistence

## Drop / interaction summary

| Gesture | Effect |
|---------|--------|
| Drop | Replace session |
| Shift+Drop | Append to session |
| Ctrl+Drop | Add onto workspace (comparison) |
| Click item | Select |
| Drag item | Move |
| Wheel | Zoom item under cursor |
| Alt+LMB / Middle | Pan view |
| Delete | Remove selected from workspace |
| Ctrl+Y | Side-by-side layout |

## Open Questions

- How session (thumbnail strip) and workspace multi-item mode best stay in sync
- Whether free rotation should be per-item only or also view-level
- Performance strategy for very large images / many concurrent items
