<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# KDDockWidgets panel shell

**Status:** required dependency; persistent central, tabbed tool docks, LayoutSaver.

## Why

Qt `QDockWidget` + `saveState`/`restoreState` is fragile (version-gated
`dockLayoutState` crashes). KDDockWidgets provides nested splits/tabs, reliable
layout serialization (`LayoutSaver`), and better floating behaviour.

## Mapping

| Qt | KDDockWidgets (QtWidgets frontend) |
|----|-------------------------------------|
| `QMainWindow` | `KDDockWidgets::QtWidgets::MainWindow` (`MainWindowOption_HasCentralWidget`) |
| `setCentralWidget` | `setPersistentCentralWidget` (Qt API is private on KD MainWindow) |
| `QDockWidget` | `KDDockWidgets::QtWidgets::DockWidget` |
| `addDockWidget(area, dock)` | `addDockWidget(dock, Location_On*)` |
| `dock->show()` / `hide()` | `dock->open()` / `close()` |
| `dock->isVisible()` | `dock->isOpen()` |
| `toggleViewAction()` | `toggleAction()` |
| `saveState` / `restoreState` | `LayoutSaver::serializeLayout` / `restoreLayout` |

Call `KDDockWidgets::initFrontend(FrontendType::QtWidgets)` once before the
main window is constructed.

## Filmstrip edge

KD has no `dockLocationChanged(Qt::DockWidgetArea)`. After open, float end, or
Move/Resize/ParentChange on the filmstrip dock, biltoo infers the edge from the
dock centre relative to the main window and sets `ThumbnailBar` horizontal
(top/bottom) or vertical (left/right). View → filmstrip edge actions still call
`addDockWidget(Location_*)` explicitly.

## Layout version

`dockLayoutVersion` was bumped when switching blob format from Qt
`saveState` to KD `LayoutSaver` — old blobs are ignored.

## Not migrated

- Dual compare (separate `ImageView`s), not a dock concern
- Content of panels (metadata, OCR, …) unchanged


## Filmstrip size

KD ignores `preferredSize` when the layout has no other items. Filmstrip is
added only after side docks exist, with `InitialOption` preferred cross-axis
extent from `ThumbnailBar::extentForThumbSize`.

## Default layout

- **Central:** `DualImageShell` via `setPersistentCentralWidget`
- **Bottom:** Filmstrip
- **Right (tabs):** Metadata, Adjustments, Crop, OCR, Text, Help
- **Left (tabs):** Layout, Contents (TOC)
- **Bottom (separate):** Messages

Reset Panel Layout re-applies these locations (tabs re-form on next cold start
from construction order if the user has not saved a layout yet).
