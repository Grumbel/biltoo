<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Application state vs configuration

| Kind | Examples | Location |
|------|----------|----------|
| **Configuration** | sticky zoom, docks, colours, slideshow | `QSettings` (XDG config) |
| **Session lists** | Recent Sessions, Bookshelf | XDG **state** JSON (`SessionListStore`) |
| **Library** | tiles, OCR | thumtoo Store |

`SessionListStore` writes `session-lists.json` under AppStateLocation / `~/.local/state/biltoo/`, debounced (~400 ms), flushed on quit. Migrates legacy QSettings arrays once.
