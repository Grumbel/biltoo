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
- [x] Toolbar with: Open, Zoom+, Zoom-, 1:1, Fit to Window, Fullscreen, Rotate Left, Rotate Right
- [x] Menu bar (File / View / Go / Help) sharing the same actions
- [x] Theme icons for toolbar and menus (with StandardPixmap fallbacks)
- [x] Hide toolbar (and menu/status/thumbnails) in fullscreen; Tab toggles toolbar
- [x] Context menu on the image view
- [x] Load single JPEG/PNG (and other formats Qt supports natively)
- [x] Simple image view with zoom, pan and 90° rotation
- [x] Command-line: accept one or more image files (+ `--fullscreen`, `--fit`, `--start-at`)
- [x] Nix flake for reproducible builds (Qt6, CMake)
- [x] REUSE / SPDX licensing (GPLv3+)
- [x] AGENTS.md for contributor / agent guidance
- [x] Basic multi-image support: list of loaded images, switch between them
- [x] Thumbnail strip at the bottom (shown when >1 image)
- [x] Keyboard navigation: Left/Right/Space/Backspace/PgUp/PgDn, R / [ ] for rotate, F for fit
- [x] Drag-and-drop of local image files onto the window
- [x] Directory open (menu + CLI): load all images in a folder, sorted by name
- [x] Status bar mouse info: image coordinates and RGB under cursor
- [x] QSettings: remember window geometry and toolbar visibility

## Near-term

- [ ] Asynchronous / background thumbnail generation (current is sync)
- [ ] Append-on-drop option vs replace-on-drop (workspace mode will need append)
- [ ] `--no-thumbnails` / `--thumbnails` CLI flags
- [ ] Sort options (name, mtime, natural, …)
- [ ] Recursive directory load (`--recursive`)
- [ ] Slideshow mode

## Workspace / Advanced View

- [ ] Treat the central area as a free workspace (QGraphicsView or custom OpenGL scene)
- [ ] Multiple images can be placed, moved, scaled and rotated independently
- [ ] Side-by-side comparison layouts
- [ ] Overlap / transparency for comparison
- [ ] Snap-to-grid or alignment helpers (optional)
- [ ] Free continuous rotation (not only 90°)
- [ ] Possibly OpenGL acceleration for smooth transforms of large images

## Metadata & Extras

- [ ] Optional side panel showing Exif / IPTC / XMP (use libexiv2 or Qt's limited support)
- [ ] Remember per-image view settings (zoom, rotation, position) in a session or on disk
- [ ] Fullscreen with optional always-hidden chrome (already partially done)

## Image Format Support

- Prototype: rely on Qt's QImageReader (JPEG, PNG, BMP, GIF, WebP if available, etc.)
- Later: ImageMagick / Magick++ for broader format support and high-quality resampling
- Consider libvips or similar for performance with very large images

## Command-line Interface

- `qimgview [options] [file|dir ...]`
- `--fullscreen` / `-f` (done)
- `--fit` (done, default behaviour)
- `--start-at=N` (done, 1-based)
- `--thumbnails` / `--no-thumbnails`
- `--exif` / `--side-panel`
- `--recursive`
- `--sort=name|mtime|...`
- Helpful man page later

## Technical Notes

- Prefer Qt6 + CMake
- Keep the codebase clean and modular (MainWindow, ImageView, ThumbnailBar, future ImageWorkspace, MetadataPanel, ...)
- Avoid hacks; if complexity grows, refactor early
- OpenGL may be needed for high-quality free rotation + zoom of multiple large images; evaluate QGraphicsView first, then QOpenGLWidget if necessary
- Icons: always `QIcon::fromTheme` + fallback so the UI stays usable without a full icon theme
- Thumbnail loading is currently synchronous — fine for small sets, needs a worker for large folders
- Mouse colour sampling keeps a full QImage in memory; for very large images this may need a different strategy

## Open Questions

- Exact interaction model for multi-image workspace (select, bring-to-front, group, etc.)
- Whether to store view state next to images (sidecar files) or in a central cache
- Performance strategy for 100+ megapixel images or dozens of images in one workspace
- Whether Tab should also toggle the menu bar / status bar, or only the toolbar
- Drop behaviour: replace session vs add to workspace
