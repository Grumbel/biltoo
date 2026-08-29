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

## Prototype Goals (v0.1)

- [x] Basic Qt6 Widgets application with main window
- [x] Toolbar, menu bar, theme icons, context menu, fullscreen chrome
- [x] Multi-image list with thumbnail strip (async load)
- [x] Keyboard navigation and slideshow (F5)
- [x] Drag-and-drop (replace; Shift+drop appends)
- [x] Add Images… (append to session)
- [x] Directory open + recursive (`-r`)
- [x] Sort by name or modification time (View menu + `--sort`)
- [x] Status bar: index, file, zoom, rotation, mouse RGB
- [x] QSettings for geometry, toolbar, sort mode, slideshow interval
- [x] CLI: `--fullscreen`, `--start-at`, `--recursive`, `--sort`, `--slideshow`,
      `--interval`, `--thumbnails`, `--no-thumbnails`
- [x] Slideshow pauses on manual navigation / thumbnail click
- [x] Nix flake, REUSE/SPDX, AGENTS.md

## Near-term

- [ ] Preferences dialog (slideshow interval, default sort, …)
- [ ] Natural/locale-aware filename sort
- [ ] Session list export / open recent

## Workspace / Advanced View

- [ ] Treat the central area as a free workspace (multiple independent items)
- [ ] Drop images onto the workspace to place them side-by-side or overlapping
- [ ] Free continuous rotation (not only 90°)
- [ ] Possibly OpenGL acceleration for smooth transforms of large images

## Metadata & Extras

- [ ] Optional side panel showing Exif / IPTC / XMP
- [ ] Remember per-image view settings (zoom, rotation, position)

## Image Format Support

- Prototype: Qt QImageReader
- Later: ImageMagick / Magick++ or libvips for broader formats and large images

## Technical Notes

- Thumbnails load on QThreadPool with a generation counter for cancellation
- Shift+drop appends; plain drop replaces
- Manual prev/next/thumbnail selection stops an active slideshow

## Open Questions

- Exact interaction model for multi-image workspace
- Sidecar vs central cache for per-image view state
- Performance strategy for very large images / many items in one workspace
