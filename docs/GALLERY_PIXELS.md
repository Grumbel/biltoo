<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery display pixels

**LQIP underlay + grid tiles.** Soft PreferCache / soft ladder whole-frame
encode is **not** used in Gallery (removed).

| Layer | Role |
|-------|------|
| **LQIP** (≤96) | Placeholder until tiles cover. Loaded from Store into ImageCache at session open (warmSessionOpenMemos). |
| **Tiles** | Sharpness for cells with on-screen long edge > 32 px. Durable Store hits preferred; encode only when coverage missing. |

Filmstrip uses the same product rule via `scheduleFilmstripTilePixels` /
`scheduleTileSynthOrPyramid` (LQIP + TileSynth). Gallery never requests soft
PreferCache for underlay.

## Open path

1. **Size (ground truth)** — batch `scheduleProbeBatch` / warm memos for the
   session; packaged layouts hold a size gate until each path has a definitive
   size or an explicit probe failure (no wall-clock timeout).
2. **Ordered placeholders + pack** — create cells only for the contiguous
   session-order prefix that already has size (or failure); extend as results
   arrive; never place out of order or with unknown/1×1 geometry.
3. **LQIP** — install from ImageCache onto blank cells (never layout authority).
4. **Tiles** — TileLoadCoordinator issues visible keys; pyramid only when durable
   coverage is missing.
5. HUD shows resolving progress (`N / M sizes`, failures, rough ETA).
