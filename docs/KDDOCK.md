<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# KDDockWidgets panel shell

**Status:** first migration cut (required dependency).

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

`QDockWidget::dockLocationChanged` is not mirrored 1:1. Edge/orientation is
updated when the filmstrip dock opens or via View filmstrip-edge actions;
revisit if KD exposes a stable location signal we can bind.

## Layout version

`dockLayoutVersion` was bumped when switching blob format from Qt
`saveState` to KD `LayoutSaver` — old blobs are ignored.

## Not migrated

- Dual compare (separate `ImageView`s), not a dock concern
- Content of panels (metadata, OCR, …) unchanged
