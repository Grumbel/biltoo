# Agent notes for QImgView

Guidance for humans and automated agents working on this codebase.

## Project intent

QImgView is a classic Qt (C++) image viewer with three presentation modes
(Image, Gallery, Workspace) on one canvas. Users browse a session, overview it
in packed layouts, or arrange several images freely for comparison. It is
*not* an image editor. See [DOMAIN.md](DOMAIN.md).

See [TODO.md](TODO.md) for the roadmap and open questions.

## Build & run

- Primary build system: **Nix flake** + CMake + Qt6.
  - `nix develop` – development shell
  - `nix build` / `nix run` – package and run
  - `nix flake check` – builds the package (compile gate)
- Manual: standard out-of-source CMake against Qt6 Widgets.
- Debug info: `nix build` uses **RelWithDebInfo** and `separateDebugInfo`
  (symbols via `nix build .#debug`). Dev shell sets `CMAKE_BUILD_TYPE=Debug`
  for local cmake builds.

Version is the top-level plain-text `VERSION` file only (e.g. `0.1.0-dev` in
git). Development builds append `.{revCount}+g{shortRev}` via the flake and
`-DPROJECT_VERSION_FULL=…`. Do not hardcode the version in CMake `project()`,
source, or the flake base string.

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

## Domain model

Canonical, implementation-independent description of modes and operations:
**[DOMAIN.md](DOMAIN.md)**. Read that before changing mode transitions, transform
targets, or session ↔ canvas sync. If code and DOMAIN.md disagree, fix the code
(or update DOMAIN.md only after explicit discussion).

## Architecture & modes

### Layers

| Layer | Owns | Role |
|-------|------|------|
| **MainWindow** | Session (`m_files`, current index), actions, slideshow, thumbnails, metadata, gallery-return memory | Application shell |
| **ImageView** | Scene, canvas items, view transform, `ViewMode`, layouts, load pipeline, HUD, undo | Presentation |
| **ImageItem** | Pixmap, path, scale/rotation/flips, opacity, z, chrome geometry | One canvas object |
| **GalleryLayout** | Packing algorithms | Pure layout |
| **ImageLoader** | Async decode | Worker |

### Mode source of truth

- **`ImageView::ViewMode`** is the only mode flag that matters.
- Shell code must query `imageView->isImageMode()` / `isGalleryMode()` /
  `isWorkspaceMode()` (or `viewMode()`). Do **not** reintroduce a parallel
  `MainWindow::m_workspaceMode` boolean.
- ThumbnailBar’s own “workspace mode” means **multi-select for session paths on
  the canvas**, not a fourth app mode; keep that name local to the strip.

### Mode table (short)

| Mode | Canvas | Transforms | Session nav / slideshow |
|------|--------|------------|-------------------------|
| **Image** | ≤1 object, path = current | Rotate/flip/reset; **view** zooms | Yes |
| **Gallery** | One object per session path, packed | None (open → Image) | No |
| **Workspace** | Free objects, subset of session | Move/scale/rotate/opacity/z | No |

### Ownership contracts

| Mode | Object scale | Object rotation | View matrix |
|------|-------------|----------------|-------------|
| Image | Typically 1; view fits/zooms | Object keeps user orientation | Fit / zoom / pan |
| Gallery | Set by layout | Neutral for packing | Scroll |
| Workspace | Object (possibly anisotropic) | Object | Scene pan/zoom |

`fitItem` must **not** clear rotation when re-fitting after rotate (DOMAIN
invariant 6).

### Transform targets

- **Image:** primary (sole) canvas item.
- **Workspace:** selection; if none and exactly one item, that item.
- **Gallery:** no transform targets.

### Mode transitions

- Gallery layout actions → `MainWindow::enterGalleryMode(layout)`.
- Open gallery cell → `showPathInImageMode(path)` (gallery-return snapshot).
- **Up** / return → `returnToGallery()` only (never Workspace).
- Workspace Mode → `enterWorkspaceMode()` / `setViewMode(Workspace)` (snapshot on leave).

### Public helpers to prefer

- `ImageView::setViewMode` / `isImageMode` / `isGalleryMode` / `isWorkspaceMode`
- `ImageView::focusSessionPath`, `targetItem`, `duplicateSelected`
- `ImageItem::isInteractive()` (workspace chrome eligibility)
- Do **not** reintroduce `MainWindow::m_workspaceMode`
- ThumbnailBar: `setMultiSelectEnabled` (not app mode); avoid new `setWorkspaceMode` call sites

### Sharp edges (do not paper over)

- Dual handle paths (item events + view-driven chrome hits) must stay consistent
  with device-space chrome painting.
- Session ↔ canvas sync differs by mode; prefer named helpers over ad-hoc
  branches.
- Action enablement should follow DOMAIN operation tables, not one-off checks.

**UI:** Gallery layouts sit with Workspace Mode on the main toolbar; Raise/Lower
and Select/Pan live on the vertical workspace tool strip.

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
  (free-form): scale at corners, rotate on all four sides; flip / raise /
  lower on the right interior; opacity along the bottom interior. Raise/Lower
  arrows stay screen-upright when the image is rotated. Hit-testing uses
  screen-pixel radii so chrome stays clickable under rotation/zoom.
  Scrollbars are hidden by default (View → Show Scrollbars).
  Preferences: General tab (slideshow / session / view) and Default application tab.
  Default application checkboxes reflect the current association (checked = QImgView
  is default); toggling applies immediately. “Set all as default” / “Remove all as
  default” cover the full list. Dialog buttons follow GNOME 2 HIG (Cancel left, OK right).

## Dialog button order (GNOME 2 HIG)

Preferences and other action dialogs use **GNOME 2 Human Interface Guidelines**
button placement:

- **Cancel** (or equivalent dismiss) on the **left**
- Affirmative action (**OK**, **Apply**, …) on the **right**
- Affirmative button is the default (Return); Esc activates Cancel

Do not rely on `QDialogButtonBox` alone for order: under many Qt styles
(e.g. Fusion) the platform hint yields Windows/KDE order (OK left of Cancel).
Build the button row explicitly (`Cancel`, stretch, `OK`) so the layout stays
HIG-conformant on every desktop.

## What not to do


- Do not add image-editing features (crop, filters, pixel changes) unless
  explicitly requested.
- Do not hard-code icon paths or ship large icon sets unless agreed.
- Do not depend on proprietary or non-free libraries.
- Do not rewrite history of previously delivered bundles.
