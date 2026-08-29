# SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later

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
- Every source file carries SPDX headers:
  ```
  // SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
  // SPDX-License-Identifier: GPL-3.0-or-later
  ```
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

## UI expectations (current)

- Classic menu bar (File / View / Go / Help).
- Toolbar with Open, Previous/Next, Zoom ±, 1:1, Fit, Rotate L/R, Fullscreen.
- Theme icons (document-open, go-previous/next, zoom-*, object-rotate-*, view-fullscreen).
- Fullscreen (F11) hides toolbar, menu bar, status bar and thumbnail bar; Tab toggles
  the toolbar while in (or out of) fullscreen. Ctrl+M toggles thumbnails.
- Context menu on the image view exposing the same core actions.
- Status bar shows [index/total], filename, dimensions, zoom %, rotation.
- Thumbnail bar at the bottom when more than one image is loaded.
- Drag-and-drop of local image files replaces the current session.
- Keyboard: Left/Right/Space/Backspace/PgUp/PgDn navigate; R / [ ] rotate; F fit.

## What not to do

- Do not add image-editing features (crop, filters, pixel changes) unless
  explicitly requested.
- Do not hard-code icon paths or ship large icon sets unless agreed.
- Do not depend on proprietary or non-free libraries.
- Do not rewrite history of previously delivered bundles.
