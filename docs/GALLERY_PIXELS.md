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
3. EMB underlay (cold free) or LQIP (warm tile side-effect) under incomplete coverage
4. Neutral placeholder           no underlay and no tiles yet
```

| Layer | What it is | When it may show |
|-------|------------|------------------|
| **Tiles** | Durable / process `TileLodRegistry` grid | Path has Succeeded tiles **or** cell is issuing. Painted by `paint_tiles_display` (live `ImageItem`) or `prepare_and_paint_cover` (virtual background when RAM already warm). |
| **EMB** | EXIF / PDF `/Thumb` ≤320 | **Cold** free floor (no source open beyond what container already has), or under tile **holes**. Must not remain the only layer when tiles exist for the path. |
| **LQIP** | ThumbHash / ≤96 | **Warm only** — free side-effect of prior tile work (`ensure_lqip` from tiles). **Never** encode LQIP on cold paths. |
| **Soft / PreferCache encode** | — | **Removed**. Warm whole-frame = TileSynth from durable tiles only. |

## One rasterizer, shared RAM

| API | Role |
|-----|------|
| `TileLodRegistry` | Process-wide **path-keyed** Succeeded tiles. Shared across Image / Gallery / Workspace / Slideshow. |
| `paint_tiles_display` | Oriented tile paint + plan overlay (`ImageItem`; also cover branch when `xform` non-identity). |
| `prepare_and_paint_cover` | Shared Gallery virtual / filmstrip / Slideshow entry. Identity `xform` → native→dest `paint_draw_plan`. Orient/crop/flip → same viewport + **`paint_tiles_display`** (payloads stay source-oriented; dest is layout space). Retained tiles **bypass** the 32px screen floor. Returns true only if tile **pixels** were drawn. |
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
   (pass session `ContentXform` when the cell has flip/turns/crop so tiles match
   the layout-sized dest; identity for unoriented paths).
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

## LQIP missing / recovery

LQIP is **never** encoded by opening the source ([thumtoo PIXEL_AND_ARCHIVE_POLICY](../../thumtoo/docs/PIXEL_AND_ARCHIVE_POLICY.md)).
It is written opportunistically during tile prepare, or via `ensure_lqip` from
**free** tile/overview data already in Store.

| Symptom | Cause | Recovery |
|---------|--------|----------|
| Process underlay empty, Store has LQIP/EMB | Seed never ran | `scheduleStoreUnderlaySeed` |
| Store missing LQIP, durable tiles exist | Kill mid-pyramid | `scheduleEnsureLqipFromTiles` / Debug → Check underlay consistency |
| Process has EMB only | Normal | Display uses EMB; Store LQIP still filled from tiles when possible |

**Debug → Check underlay consistency** compares process ImageCache vs Store and
logs issue lines to stderr; schedules seed/ensure on problems.

Store writes (`put_blob_lqip`) are single SQLite statements under WAL — a process
kill cannot leave a half-row; at worst the LQIP row is absent and ensure retries.
