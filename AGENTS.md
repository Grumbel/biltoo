# Agent notes for Biltoo

Guidance for humans and automated agents working on this codebase.

## Project intent

Biltoo is a classic Qt (C++) image viewer with three presentation modes
(Image, Gallery, Workspace) on one canvas. Users browse a session, overview it
in packed layouts, or arrange several images freely for comparison. It is
*not* an image editor. See [DOMAIN.md](DOMAIN.md).

See [TODO.md](TODO.md) for the roadmap and open questions.
Latest agent handoff: **TODO.md → biltoo-179-opengl-drawForeground**
(OpenGL viewport; overlays in drawForeground).
Next bundle number: **180**.

**Identity (mandatory):** [IDENTITY.md](IDENTITY.md) — `SessionImageId` is the
only appearance/crop key. Path is decode source only. Before any crop, flip,
drop, or filmstrip change, read §0, §13 (crop lock), and §14 (id allocation).

**Handoff:** [SESSION.md](SESSION.md) — SessionImageId model, what shipped
in the chrome/identity series, residual risks, and how to continue.

**Portability:** [PORTABILITY.md](PORTABILITY.md) — Linuxisms vs portable Qt core;
Windows / cross-compile feasibility (not a scheduled port).

## Build & run

- Primary build system: **Nix flake** + CMake + Qt6.
  - `nix develop` – deps only (compiler, Qt, vips, …). Not a mini install.
  - In the shell: `biltoo-configure`, `biltoo-build`, `biltoo-run`
    (out-of-tree default: `/tmp/biltoo-build`, override with `BILTOO_BUILD_DIR`)
  - First time: `biltoo-configure` then `biltoo-run`
  - `nix build` / `nix run` – packaged, `wrapQtAppsHook`-wrapped binary
  - `nix flake check` – package + CMake tests


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
- Deliverables are sequential git bundles (`biltoo-001-…`, `biltoo-002-…`, …)
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
| **Gallery** | One object per session path, packed | Rotate/flip selection | No |
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
- **Gallery:** selected tiles for ±90° rotate and flip; no free move/scale chrome.

### Mode transitions

- Gallery layout actions → `MainWindow::enterGalleryMode(layout)`.
- Open gallery cell → `showPathInImageMode(path)` (gallery-return snapshot).
- Open workspace item (double-click body) → `showPathInImageMode(path)` (workspace-return).
- **Up** / top-edge / Esc → `returnFromImageMode()` (Gallery or Workspace, matching open origin).
- Workspace Mode → `enterWorkspaceMode()` / `setViewMode(Workspace)` (snapshot on leave).

### Public helpers to prefer

- `ImageView::setViewMode` / `isImageMode` / `isGalleryMode` / `isWorkspaceMode`
- `ImageView::focusSessionPath`, `targetItem`, `duplicateSelected`
- `ImageItem::isInteractive()` (workspace chrome eligibility)
- Do **not** reintroduce `MainWindow::m_workspaceMode`
- Prefer `ImageView::setViewMode`; do not add back `ImageView::setWorkspaceMode(bool)`
- ThumbnailBar: `setMultiSelectEnabled` (not app mode); avoid new `setWorkspaceMode` call sites

### Sharp edges (do not paper over)

- **Fullscreen shortcuts:** menu/toolbar are hidden in fullscreen. Every
  `QAction` with a key sequence must be registered on the main window via
  `bindViewerShortcuts()` (`addAction` + `Qt::ApplicationShortcut`). Do not
  rely on menu-only WindowShortcut for viewer keys (H, Space, Ctrl+F, …).
- Transform chrome input is **owned by ImageView only** (viewport hits +
  paintEvent). ImageItem must not begin handle drags or set chrome cursors.
- Session ↔ canvas sync differs by mode; prefer named helpers over ad-hoc
  branches.
- Action enablement should follow DOMAIN operation tables, not one-off checks.

### Workspace transform chrome (scale-invariant handles) — **fragile, re-read**

Normative behaviour, coordinate spaces, near-zero scale, and drag semantics:
**[HANDLES.md](HANDLES.md)**. Read that before changing placement, paint, or
scale-drag math.


Handles **must stay a constant size on screen** under any combination of:

- view zoom (`QGraphicsView::transform`)
- item scale / rotation / anisotropic stretch
- HiDPI device pixel ratio

This has regressed more than once. When touching chrome code, treat the following
as **hard constraints**, not style preferences:

1. **Draw chrome in viewport device pixels**, not scene/item local units.
   Current path: `ImageView::paintEvent` → `ImageItem::paintInteractionChrome`
   (same space as edge affordances and the HUD). Do **not** draw handles from
   `ImageItem::paint` under the item transform, and do **not** rely on
   `drawForeground` with a scene transform for final size. Prefer
   `view->mapFromScene(item->mapToScene(local))` for centres; derive arm
   directions from local axes mapped the same way so brackets **rotate with
   the image**. Do not call `setWorldTransform(identity)` in ways that break
   HiDPI painter state — map explicitly and draw in viewport logical pixels.
2. **Viewport update mode must stay `FullViewportUpdate`** while overlays are
   painted in `paintEvent`. `SmartViewportUpdate` scrolls/blits pixels and
   smears HUD/chrome across the image (seen on Vertical gallery scroll).
3. **Logical centres** live in item local space (`handleCenter`); map to the
   viewport with `mapToScene` + `QGraphicsView::mapFromScene` before drawing.
4. **On-screen sizes** (`kHandleScreenPx`, edge thickness, chrome buttons) are
   constants in *viewport pixels*. Never multiply them by item scale or view
   scale when stroking. Inverse scale (`/ screenScale()`) is only for placing
   centres *in local space* (e.g. rotate offset, button stack).
5. **Hit-testing** compares **view-pixel** distances to those centres
   (`handleDistanceScreenPx` / edge segment projection), not local radii alone.
   The view owns press/hover so rotated/covered chrome stays reachable.
6. If handles look tiny or huge after a change, the mapping or paint space is
   wrong — fix the math; do not paper over with ad-hoc scale factors.

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
- Drag-and-drop of local image files: always appends to the session in Image mode
  (File → Open still replaces). Paths already in the session only change the
  current index. In workspace mode drop appends to the session
  (thumbnail bar) and places the images on the canvas.
- Keyboard: Left/Right/Space/Backspace/PgUp/PgDn navigate; R / [ ] rotate; F fit.
- Open Directory (Ctrl+Shift+O) and CLI directories expand to images sorted by name.
- `--recursive` / `-r` walks subdirectories when expanding directory arguments.
- Status bar shows image coordinates and RGB under the cursor.
- Window geometry and toolbar visibility are restored via QSettings.
- Slideshow (F5): advances automatically; `--slideshow` and `--interval=ms` on the CLI.
  Manual navigation or thumbnail click pauses the slideshow. Optional auto-fullscreen
  (Preferences, on by default). Disabled in workspace mode.
- CLI `--mode=image|gallery|workspace` selects the start presentation (Gallery uses
  masonry). Overrides Preferences “Start in workspace mode” for that launch only.
- Thumbnails are decoded asynchronously on the global QThreadPool.
- Sort by name or mtime (View menu; `--sort=name|mtime`).
- Open replaces the session; Add Images… / Shift or Ctrl+drop appends (deduplicated).
- `--thumbnails` / `--no-thumbnails` force thumbnail bar visibility.
- Workspace mode is optional and off by default (View → Workspace Mode).
  Image mode: left-drag pans, single centred non-interactive image.
  Workspace canvas is snapshotted when disabling the mode and restored when
  enabling again. Select/Pan tools on a left toolbar; Undo/Redo for moves.
  In workspace mode the thumbnail bar uses normal multi-select (click / Ctrl /
  Shift). Double-click toggles canvas membership; drag onto the canvas adds or
  moves at the drop point. Each image remembers position, scale and rotation
  while off-canvas. Selected items show scale and rotate handles
  (free-form): scale at corners, rotate on all four sides; flip / raise /
  lower on the right interior; opacity along the bottom interior. Raise/Lower
  arrows stay screen-upright when the image is rotated. Hit-testing uses
  screen-pixel radii so chrome stays clickable under rotation/zoom.
  Scrollbars are hidden by default (View → Show Scrollbars).
  Preferences: General tab (slideshow / session / view) and Default application tab.
  Default application checkboxes reflect the current association (checked = Biltoo
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

## Session vs canvas (quick)

- **Gallery Delete** removes paths from the **session** (undoable). Layout switches use `m_files` only.
- **Workspace Delete** removes from the **canvas** only; session membership stays.
- Gallery pack order follows session/`setWorkspacePaths` order, not async decode completion.
- Gallery wheel scrolls only; do not add view-zoom on wheel in Gallery.
