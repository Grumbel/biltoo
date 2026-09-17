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

## Image mode

1. **Fit** — soft underlay only; no tile spam in `THUMTOO_DEBUG` (screen long edge ≤ ~512).
2. **Zoom in** past soft (~512 on-screen long edge) — tiles request at target scale; soft stays underlay until cells arrive.
3. **Continuous wheel zoom** — no full intermediate grids every notch (scale hold ~150ms); large jumps still commit immediately.
4. **Pan** while zoomed — neighbouring cells prefetch (margin); obsolete in-flight cancelled.
5. **Retina / DPR > 1** — finer scales requested than on a 1× display at the same logical zoom.
6. PreferCache whole-frame climb should **not** run while in the tile band (`m_tileLodPreferCancelled`).

## Workspace

1. Two items, **same path**, zoom both — second item should reuse RAM tiles (shared registry).
2. Selection vs up-to-8 bound still ticks.

## Negative

- Gallery packed cells: still soft-only (no grid tile path).
- Crop draft locked path: no tile/Prefer climb (existing freeze).

## Debug

```bash
export THUMTOO_DEBUG=1
# optional: biltoo load traces
export BILTOO_LOAD_DEBUG=1
```


## Content orient / crop

Axis-aligned flip / 90° / crop: tiles should track via ContentXform maps.
Free-rotated crop: tile LOD off (soft / PreferCache only).


## Gallery

1. Pack at normal size — soft only.
2. Ctrl+wheel zoom until a cell is large on screen — tiles should appear for
   that cell when a pyramid exists.
