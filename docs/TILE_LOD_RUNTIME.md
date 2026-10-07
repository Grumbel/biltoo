<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile LOD — runtime verification checklist

Manual checks after building biltoo with thumtoo and a **prepared tile pyramid**.

## Prepare

```bash
# Example: build pyramid for a large photo
thumtoo-prepare --tiles /path/to/large.jpg
# or a directory
thumtoo-prepare --tiles ~/Pictures/big/
```

Open the same path in biltoo (Image mode). Soft/LQIP may still appear first.

**Without a prepared pyramid**, deep zoom stays on PreferCache — the tile band is
not claimed (`tileLodWanted` requires `hasDurableTiles`).

## Image mode

1. **Fit** — soft underlay only; no tile spam in `THUMTOO_DEBUG` (screen long edge ≤ ~512).
2. **Zoom in** past soft (~512 on-screen long edge) — tiles request at target scale; soft stays underlay until cells arrive.
3. **Continuous wheel zoom** — demand changes coalesce for 24 ms before issue; cells of abandoned intermediate scales are cancelled (Queued → Cancelled → Missing), never left InFlight.
4. **Pan** while zoomed (after the view was fully covered) — neighbouring cells request without needing another zoom; margin prefetch; obsolete in-flight cancelled.
5. **Scrollbar drag** while zoomed — same as pan (visible set updates).
6. **Retina / DPR > 1** — finer scales requested than on a 1× display at the same logical zoom.
7. PreferCache whole-frame climb should **not** run while in the tile band (`m_tileLodPreferCancelled`).
8. **Failed cells** — retried after 0.5 s and 1 s, then a 30 s cooldown; the item shows "Showing lower resolution: <reason>" (parents cover) or "Tiles failed: <reason>" (holes). `Unavailable` cells (outside grid, finer than a raster PDF page needs) are never retried. A stall (no reply in 45 s) logs `biltoo/tile: STALL`.
8b. **PDF pages** (benchtoo `pdf-classes`, Panels → Status open) — a scan stops
   refining at its profile's cap (300 dpi → scale −1) with the decision and
   reason listed; vector and mixed pages (text, Bates stamp on a scan) keep
   zooming to −4. Status shows one image decode per zoom level and display
   list built once; no "Unavailable" cells during normal zoom.
9. **Zoom out** below soft max — tile requests stop; PreferCache may resume.
10. **A→B→A path switch** (Image ←/→) — tiles for A remain in the global path cache
    after leaving A; returning to A should paint from RAM without a full rebuild
    (until global 384 MiB LRU eviction of idle paths, or the **64 idle-path**
    cap drops oldest zero-ref entries). `pathRam=K` in `BILTOO_TILE_DEBUG` item
    lines should be >0 immediately after return; soft may still swap first.
11. **Neighbor prefetch** — after quiet settle on index *i*, ±1 session neighbors
    should receive overview tile requests that are **pumped until completions
    land** (when durable pyramid is known); stepping to a neighbor should show
    coarse tiles sooner than a cold path. Already-warm neighbors (≥4 Succeeded
    in path RAM) skip issue. Optional env: `BILTOO_TILE_RAM_MIB`, `BILTOO_TILE_MAX_IDLE`.
12. **Reload** — `purgeTilePathRam` must clear grid paint for the path; after
    reload, tiles rebuild (no pre-reload cells). Workspace: two items same path
    both clear.

## Workspace

1. Two items, **same path**, zoom both — second item should reuse RAM tiles (shared registry).
2. Selection vs up-to-8 bound still ticks.
3. Pan / scrollbar while deep-zoomed refills the grid (same as Image mode).

## Gallery

1. Pack at normal size — soft only (on-screen cell ≤ ~512 device px).
2. Ctrl+wheel zoom until a cell is large on screen (> ~512 device px) **and** a pyramid exists — tiles should appear for that cell.
   Threshold uses **scene cell × view scale × DPR** (not content × item×view).
3. Scroll after coverage — decode window refresh + tile tick for oversized cells.
4. Many large cells (Ctrl+wheel pack): in-view cells get tile budget before off-screen.

## Crop draft

1. Enter crop draft — tiles suppressed (`setTileLodSuppressed`); PreferCache locked for that path.
2. Leave draft — tiles can resume when still past soft max.

## Content orient / crop

Flip / 90° / axis-aligned crop / free-rotated crop: tiles via ContentXform maps
(AABB of corners for free rot). Soft remains underlay until cells arrive.
Free-rot paint uses the same centre/`rotate(-θ)` transform as `materializeDisplay`.

## Color grade

Non-identity grade is applied on tile resolve (per-item QImage cache). Identity
conversions are also cached so paint does not re-copy every cell each frame.

## Mid-session pyramid

If tiles are prepared while biltoo is already deep-zoomed, `durableTilesReady`
should wake the tile tick without requiring a zoom gesture.

## Debug

```bash
export THUMTOO_DEBUG=1
export BILTOO_TILE_DEBUG=1   # tile-sched per pump + tile-coord item phase/counts/error
# optional: biltoo load traces
export BILTOO_LOAD_DEBUG=1
```
