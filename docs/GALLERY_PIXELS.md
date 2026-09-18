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

1. warmSessionOpenMemos — size memo + LQIP into ImageCache + durable-tile memo (worker, joined before pack).
2. Pack with real aspects when sizes warm.
3. Install LQIP from ImageCache onto blank cells.
4. TileLoadCoordinator issues visible tile keys (Gallery uses a higher per-tick budget than Image mode).
5. scheduleTilePyramid only when durable coverage is not already known.
