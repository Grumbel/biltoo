<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery display pixels

**Authoritative stack for what you see in Gallery cells.** Soft whole-frame
ladder is not a product path ([KILL_SOFT.md](KILL_SOFT.md)).

## Priority (top wins)

```text
1. Exact / parent 256² tiles   ← product display when path has Succeeded tiles
2. Coarser retained tiles        (same TileLodRegistry, min_scale / overview)
3. EMB or LQIP underlay          only cold, or holes under incomplete coverage
4. Neutral placeholder           no underlay and no tiles yet
```

| Layer | What it is | When it may show |
|-------|------------|------------------|
| **Tiles** | Durable / process `TileLodRegistry` grid | Path has Succeeded tiles **or** cell is issuing. Painted by `paint_tiles_display` (live `ImageItem`) or `prepare_and_paint_cover` (virtual background when RAM already warm). |
| **EMB** | EXIF / PDF `/Thumb` ≤320 | **Cold only** as full-cell floor, or under tile **holes**. Must not remain the only visible layer when registry already has tiles for that path. |
| **LQIP** | ThumbHash / ≤96 | Same role as EMB; often replaced in cache when a larger EMB arrives. |
| **Soft / PreferCache** | — | **Removed** in Gallery. |

## One rasterizer, shared RAM

| API | Role |
|-----|------|
| `TileLodRegistry` | Process-wide **path-keyed** Succeeded tiles. Shared across Image / Gallery / Workspace / Slideshow. |
| `paint_tiles_display` | Oriented tile paint + plan overlay (`ImageItem`). |
| `prepare_and_paint_cover` | Identity native→dest cover (Slideshow, virtual warm floor). Retained tiles **bypass** the 32px screen floor so min-scale overview still paints while scrolling. |
| `tileLodActive()` | True if this controller has tiles **or** retained path RAM. |
| `tileLodHasPathRam()` | Registry (or controller retained) has Succeeded tiles for the path. |

## Paint order (live `ImageItem`)

1. `prepareTileLodPlan()` — bind session, adopt retained path tiles, set viewport.
2. Underlay (EMB/LQIP) if coverage incomplete or still cold.
3. `paint_tiles_display` — plan cells from shared cache (must run when path has tiles).
4. Plan debug overlay when enabled.

**Bug this document guards against:** treating `tileLodActive()` as session-only
(`hasAnyTile()` without retained RAM) so paint drew full-cell EMB while the
registry already held the pyramid.

## Virtual slots (drawBackground)

Background under live items. Order:

1. If registry has Succeeded tiles for the path → `prepare_and_paint_cover`
   (min_scale) + optional plan overlay; `touch()` path for LRU.
2. Else EMB/LQIP from `ImageCache::getUnderlay`.
3. Else neutral placeholder.

## Open / scroll

1. Size gate → definitive sizes.
2. SizeReply seeds underlay into `ImageCache`.
3. Visible window materializes live items; warm paths preferred.
4. Coordinator ticks tiles; registry retains Succeeded tiles across scroll.

Filmstrip uses TileSynth whole-frame into `ImageCache` for strip icons (same
Store, not grid paint on the strip widget).
