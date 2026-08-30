# Agent notes for QImgView

Guidance for humans and automated agents working on this codebase.

## Project intent

QImgView is a classic Qt (C++) image viewer. The central idea is that the
image area behaves as a **workspace**: users can open one or many images,
drop additional images onto the view, and freely scale / rotate / arrange
them for comparison. It is *not* an image editor.

See [TODO.md](TODO.md) for the roadmap and open questions.

## Build & run

- Primary build system: **Nix flake** + CMake + Qt6.
  - `nix develop` – development shell
  - `nix build` / `nix run` – package and run
- Manual: standard out-of-source CMake against Qt6 Widgets.

Do not introduce qmake `.pro` files or Qt5-only APIs.

## Coding conventions

- C++17, Qt6, CMake.
- Source and build files carry SPDX headers:
  ```
  // SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
  // SPDX-License-Identifier: GPL-3.0-or-later
  ```
  Documentation (`README.md`, `AGENTS.md`, `TODO.md`, `REUSE.md`, …) does **not**
  use in-file SPDX headers; licensing is described in [REUSE.md](REUSE.md) and
  covered by [`.reuse/dep5`](.reuse/dep5).
- Prefer clear, modular classes (MainWindow, ImageView / future workspace,
  ThumbnailBar, MetadataPanel, …). Refactor early when complexity grows.
- No silent feature removal. Discuss before dropping behaviour.
- Avoid quick hacks; prefer the proper design even if it takes longer.
- Icons: use `QIcon::fromTheme(...)` with a `QStyle::StandardPixmap`
  fallback so the app works under any icon theme and without a theme.

## Git & delivery

- Commits are small and task-focused.
- Author: `Ingo Ruhnke <grumbel@gmail.com>`
- Always add the trailer:
  ```
  Co-authored-by: Grok <grok@x.ai>
  ```
- Deliverables are sequential git bundles (`qimgview-001-…`, `qimgview-002-…`, …)
  that stack cleanly on the previous tip and use `HEAD` as the ref.
  Bundle numbers never repeat.

## UI modes (terminology)

Use these names consistently in code comments, menus, and docs:

| Term | Meaning |
|------|---------|
| **Image mode** (also “classic mode”) | Single-image viewing. One centred image; edge click / Go menu navigates the session; slideshow; no multi-item free placement. |
| **Workspace mode** | Free-form multi-image canvas only. Thumbnail bar toggles membership; items remember position/scale/rotation off-canvas. Scale/rotate handles, opacity, z-order. View zoom pans/scales the *view*. |
| **Gallery layouts** | Packaged arrangements: Side-by-Side, Vertical, Grid, Stack, Masonry. Items are not freely moved; view transform stays identity. Double-click opens **Image mode**; **Back to Gallery** returns to the same layout. |
| **Free-form layout** | The only layout used by Workspace mode (user placement). |

**Gallery ↔ Image:** choosing a gallery layout enters multi-image mode with that packing. Double-click opens Image mode for that file; View → Back to Gallery restores the layout (selects all session thumbs on the canvas). Workspace Mode toggle forces free-form.

## UI expectations (current)

- Classic menu bar (File / View / Go / Help).
- Toolbar with Open, Previous/Next, Zoom ±, 1:1, Fit, Rotate L/R, Fullscreen.
- Theme icons (document-open, go-previous/next, zoom-*, object-rotate-*, view-fullscreen).
- Fullscreen (F11) hides toolbar, menu bar, status bar and thumbnail bar; Ctrl+T toggles the toolbar; Ctrl+M toggles thumbnails. Esc exits fullscreen.
- Context menu on the image view exposing the same core actions.
- Status bar shows [index/total], filename, dimensions, zoom %, rotation.
- Thumbnail bar at the bottom when more than one image is loaded.
- Drag-and-drop of local image files: plain drop replaces the session in classic
  mode; Shift/Ctrl+drop appends. In workspace mode drop appends to the session
  (thumbnail bar) and places the images on the canvas.
- Keyboard: Left/Right/Space/Backspace/PgUp/PgDn navigate; R / [ ] rotate; F fit.
- Open Directory (Ctrl+Shift+O) and CLI directories expand to images sorted by name.
- `--recursive` / `-r` walks subdirectories when expanding directory arguments.
- Status bar shows image coordinates and RGB under the cursor.
- Window geometry and toolbar visibility are restored via QSettings.
- Slideshow (F5): advances automatically; `--slideshow` and `--interval=ms` on the CLI.
  Manual navigation or thumbnail click pauses the slideshow. Optional auto-fullscreen
  (Preferences, on by default). Disabled in workspace mode.
- Thumbnails are decoded asynchronously on the global QThreadPool.
- Sort by name or mtime (View menu; `--sort=name|mtime`).
- Open replaces the session; Add Images… / Shift or Ctrl+drop appends (deduplicated).
- `--thumbnails` / `--no-thumbnails` force thumbnail bar visibility.
- Workspace mode is optional and off by default (View → Workspace Mode).
  Image mode: left-drag pans, single centred non-interactive image.
  Workspace canvas is snapshotted when disabling the mode and restored when
  enabling again. Select/Pan tools on a left toolbar; Undo/Redo for moves.
  In workspace mode the thumbnail bar toggles membership: click selects or
  deselects an image on the canvas; each image remembers position, scale and
  rotation while off-canvas. Selected items show scale and rotate handles
  (free-form) and flip / raise / lower chrome on the right interior of the
  image plus an opacity track along the bottom interior (item-local, rotates
  with the image). Scrollbars are hidden by default (View → Show Scrollbars).
  Preferences: General tab (slideshow / session / view) and Default application tab.

## What not to do

- Do not add image-editing features (crop, filters, pixel changes) unless
  explicitly requested.
- Do not hard-code icon paths or ship large icon sets unless agreed.
- Do not depend on proprietary or non-free libraries.
- Do not rewrite history of previously delivered bundles.
