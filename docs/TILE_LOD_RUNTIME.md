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
3. **Continuous wheel zoom** — no full intermediate grids every notch (scale hold ~150ms); large jumps still commit immediately.
4. **Pan** while zoomed (after the view was fully covered) — neighbouring cells request without needing another zoom; margin prefetch; obsolete in-flight cancelled.
5. **Scrollbar drag** while zoomed — same as pan (visible set updates).
6. **Retina / DPR > 1** — finer scales requested than on a 1× display at the same logical zoom.
7. PreferCache whole-frame climb should **not** run while in the tile band (`m_tileLodPreferCancelled`).
8. **Failed cells** (missing pyramid levels) — no continuous re-request spam on a stable viewport; a plan change (pan/zoom) may retry.
9. **Zoom out** below soft max — tile requests stop; PreferCache may resume.
10. **A→B→A path switch** (Image ←/→) — tiles for A remain in the global path cache
    after leaving A; returning to A should paint from RAM without a full rebuild
    (until global 384 MiB LRU eviction of idle paths).
11. **Neighbor prefetch** — after quiet settle on index *i*, ±1 session neighbors
    should receive a small overview tile issue (when durable pyramid is known);
    stepping to a neighbor should show coarse tiles sooner than a cold path.

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
export BILTOO_TILE_DEBUG=1   # path / covered / active per tile tick
# optional: biltoo load traces
export BILTOO_LOAD_DEBUG=1
```
