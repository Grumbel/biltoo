<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Research: kTileOverlap (257) paint math and lower-zoom gaps

**Status:** research only (2026-09-25). No code change in this note.
**Trigger:** reports that the 257 overlap path “eats lines” and that **lower
zoom levels miss pieces**. Suspected wrong host-side math.

## 1. Intended contract (thumtoo + biltoo)

| Constant | Value | Meaning |
|----------|-------|---------|
| `kTileSize` | 256 | Exclusive grid step in **level** (scale-space) pixels |
| `kTileOverlap` | 1 | Extra column/row on **right and bottom** of each cell |

Thumtoo (`tile_cell_pixel_rect`):

- Origin: `(x * 256, y * 256)` on the level image of size `(sw, sh)`.
- Interior exclusive size: `min(256, remaining)`.
- Payload: `min(interior + 1, remaining)` — overlap only when the level has
  another pixel past the exclusive edge.
- Grid step stays 256; legacy 256×256 Store tiles remain valid (no expand).

Biltoo host paint (design intent from tips 257 / 2676 series):

1. `dst_content` from `tile_content_rect` = **exclusive** content coverage.
2. **ExactTile** only: if bitmap is larger than exclusive level size, **expand
   dest right/bottom** by the extra strip, scaled into content units by
   `factor = 2^scale`.
3. **CoarserTile**: do **not** expand; sample parent with `parent_uv_for_child`,
   which maps into the exclusive first 256 columns/rows of a 257 parent
   (`map_w = parent_w - 1` when `parent_w > 256`).
4. Paint order L→R, T→B so overlap strips win.
5. Additional `±0.75 / device_per_content` dest inflate to hide QPainter
   subpixel hairlines.

JPEG still encodes each cell independently — the shared column is not bit-identical
after decode. Overlap only helps bilinear continuity at the join; it does not
fix independent quantisation.

## 2. Content ↔ level mapping

```text
factor = 2^scale
step  = 256 * factor          # content pixels for one exclusive cell
tile (s,x,y) exclusive content:
  [x*step, (x+1)*step) ∩ [0, content_w) × same for y
```

Level size uses successive **integer floor-half** (same as thumtoo):

```text
dim_at_tile_scale(n, s): n = n/2, s times
```

This is **not** the same as “each level pixel covers exactly `2^s` content
pixels” at the image edge when `content_w` is odd under repeated floor-half.

## 3. ExactTile expand — verified for interior cells

For a full interior cell at scale `s` with payload 257×257:

| scale | exclusive content | expand by | expanded dest width |
|------:|------------------:|----------:|--------------------:|
| 0 | 256 | 1 | 257 |
| 1 | 512 | 2 | 514 |
| 2 | 1024 | 4 | 1028 |

Neighbor exclusive starts at `step`. Expanded dest ends at `step + factor`.
Overlap in content space = exactly **one level pixel** (`factor` content px).
**Interior expand math matches the thumtoo payload.**

Edge cells with no room for overlap (payload == exclusive) do not expand.
Simulations for many widths (300…1000) show expanded dest ends at `content_w`
(no overshoot) when payload is consistent with thumtoo’s `tile_cell_pixel_rect`.

**ImageItem** additionally intersects expanded `srcBox` with the native content
rect; `paint_draw_plan` does not. For correct payloads this only matters if
expand would pass the image edge (should not, for last exclusive cell).

## 4. Parent UV — verified for the tested cases

`parent_uv_for_child` maps fine exclusive content rect into parent exclusive
pixel space (`map_w = parent_w - overlap` when 257). Unit test
`test_parent_uv_edge_tile` / `test_fallback_parent_uv` cover non-pow2 and
quadrant split.

Sampling the parent’s +1 seam strip as if it were exclusive content would
stretch the wrong column into a fine cell — that path was fixed (map into 256).
**Parent UV design is consistent with “overlap is ExactTile-only”.**

## 5. Confirmed math defect: exclusive content coverage gaps at scale > 0

`tile_content_rect` covers content with steps of `256 * 2^s` clamped to
`content_w`. The **level** only has `dim_at_tile_scale(content_w, s)` columns.
When floor-half drops a remainder, the last content pixel(s) are **not** in any
tile’s exclusive content rect.

Examples (simulation):

| content_w | scale | level dim | tiles | content covered | **gap** |
|----------:|------:|----------:|------:|----------------:|--------:|
| 513 | 0 | 513 | 3 | 513 | 0 |
| 513 | 1 | 256 | 1 | **512** | **1** |
| 1025 | 1 | 512 | 2 | 1024 | 1 |
| 1025 | 2 | 256 | 1 | 1024 | 1 |
| 4097 | 1..4 | … | … | 4096 | 1 |
| 2052 | 3 | 256 | 1 | 2048 | 4 |

Pattern: whenever `content_w - dim_at(content_w,s) * 2^s > 0`, exclusive
coverage leaves a **right/bottom strip of content with no ExactTile dest**.

Effects at **lower zoom** (higher `target_scale`):

- Planner only emits keys for tiles that exist on the level; dests stop short of
  the image edge by 1–few content pixels.
- Soft/LQIP underlay may hide the strip when still drawn; when
  `tileLodViewportCovered` suppresses underlay, the strip can show as a
  **missing edge** or hairline against background.
- This is **independent of kTileOverlap=1** in origin (it comes from step =
  `256*2^s` vs floor-half level size). Overlap expand does not close it: the
  last cell often has **no** +1 payload (`payload == interior` at the level
  edge), so no expand.

This is a strong candidate for “lower zoom levels … missing pieces” (especially
along the right/bottom image edge).

## 6. Device-pixel overdraw vs zoom

```text
gap_content = 0.75 / device_per_content   # each side of every cell
```

| dpc (zoom) | gap in content px (±) |
|-----------:|----------------------:|
| 4.0 (~400%) | 0.19 |
| 1.0 (100%) | 0.75 |
| 0.5 (50%) | 1.5 |
| 0.25 (25%) | 3.0 |
| 0.1 (10%) | 7.5 |
| 0.05 (5%) | 15.0 |

At low zoom, every tile dest is inflated by many **content** pixels. That is
intentional for subpixel hairlines at high zoom, but when zoomed out it:

- Overdraws far past the exclusive boundary (on top of ExactTile +1 expand).
- Interacts with clip-to-contentRect: expansion outside the item is clipped;
  internal overlaps just composite.
- Does **not** by itself open gaps between exclusive cells; it closes them.
  It can, however, make seams and soft underlay edges look like **lines are
  being eaten** (neighbor overwrites a wide band).

Whether this is the user’s “eating up lines” needs runtime confirmation
(compare with `gap = 0` and with overdraw disabled).

## 7. Interaction of expand + overdraw + JPEG

At high zoom (scale 0): expand +1 content px + ~0.75 device px overdraw.
Seam quality still limited by **independent JPEG** of the shared column
(documented residual in TODO / TILE_LOD).

At coarse ExactTile scales: expand is `factor` content px (2, 4, 8…) plus large
content gap overdraw when dpc is small. Upscaled coarse JPEG makes the
shared strip divergence more visible — can look like a bright/dark **line**
along the grid even when geometry abuts.

## 8. Historical bug (already fixed on origin)

`CoarserTile` briefly expanded dest from **parent** bitmap width (tip before
`8e62ea3`). That blew stand-in geometry (cells far too large / wrong place).
Current code expands **ExactTile only**. If a build mixes paths, verify both
`paint_draw_plan` and `ImageItem::paint`.

## 9. Hypotheses ranked (for the reported symptoms)

| Rank | Symptom fit | Mechanism |
|-----:|-------------|-----------|
| 1 | Missing pieces at **lower zoom**, especially image **right/bottom** | Exclusive `tile_content_rect` vs floor-half level size → content not covered by any Exact dest; underlay may be suppressed when “covered” |
| 2 | “Lines” at tile grid, worse when zoomed out / coarse | JPEG dual-encode of overlap + large content-space overdraw + upscaled coarse tiles |
| 3 | Missing interior pieces at lower zoom | Parent UV / failed cells / coverage lying “fully covered” with holes (separate from 257 math; check plan + soft) |
| 4 | Expand applied to CoarserTile | Should be fixed; re-check if any paint path still expands non-Exact |

**Not supported by math:** interior ExactTile expand formula being off by a full
line for full 257 cells (simulations match contract).

## 10. What a correct fix would need (do not implement in this note)

1. **Coverage mapping:** define exclusive content coverage from the **level
   pixel grid** (each level pixel → content rect via consistent inverse of
   floor-half), or extend the last tile’s content rect to `content_w/h` so
   scale>0 tiles always cover the native AABB. Must stay consistent with
   thumtoo’s `tile_cell_pixel_rect` and encode.
2. **Overdraw policy:** keep ~1 device-px overdraw at high dpc; **clamp**
   content-space gap so zoomed-out views do not overdraw many content pixels
   (e.g. `min(0.75/dpc, 0.5)` or similar). Separate knob from overlap expand.
3. **Seam quality:** accept JPEG residual, or longer-term shared-edge encode /
   lossless seam strip (thumtoo policy change).
4. **Tests:** content coverage ∀ scales for odd widths (513, 1025, 4097);
   Exact expand interior/edge; parent UV with 257 parents; overdraw clamp.

## 11. Files involved

| Area | Path |
|------|------|
| Constants | `src/tilelod/tile_types.hpp`, thumtoo `include/thumtoo/constants.hpp` |
| Content rect / parent UV | `src/tilelod/lod_math.hpp` |
| Draw plan | `src/tilelod/draw_plan.cpp` |
| Cover paint expand | `src/tilelod/tile_painter.cpp` |
| ImageItem expand + overdraw | `src/imageitem_interaction.cpp` |
| Tests | `tests/tilelod_test.cpp` |
| Docs | `docs/TILE_LOD.md`, thumtoo `TILES.md` |

## 12. Simulation appendix (selected)

```text
cw=513 s=1: tiles=1 covered=[0,512) gap=1
cw=1025 s=1: covered=[0,1024) gap=1
cw=2052 s=3: covered=[0,2048) gap=4

Interior expand s=0..2: exclusive + factor content px (matches 257 payload)
Edge last tile: overshoot 0 when payload matches thumtoo
dpc=0.1 → ±7.5 content px overdraw per cell edge
```

## 13. Implemented (biltoo-2679)

1. **Last-tile content rect** — `tile_content_rect` extends the last column/row
   to `content_w` / `content_h` so exclusive coverage is the full native AABB
   at every scale. Out-of-grid keys return empty.
2. **Overdraw clamp** — `paint_seam_overdraw_content(dpc)` = min(0.75/dpc, 1.0);
   used by `paint_draw_plan` and ImageItem tile paint.
3. **Tests** — `test_content_coverage_odd_widths`, `test_paint_seam_overdraw_clamp`.

JPEG dual-encode residual is unchanged.

## 14. Implemented (biltoo-2680) — paint order was inverted

Expand and overdraw grow the dest on **right/bottom**. Sorting **L→R / T→B**
painted the *next* exclusive cell last, so it erased the shared strip. With
symmetric `±gap`, the next cell also overwrote ~0.75 content px of the previous
exclusive rect (“disappearing lines” on the grid).

**2680:** sort **R→L / B→T**; apply overdraw only as `dst.adjust(0,0,+gap,+gap)`.

## 15. Correct QPainter model (biltoo-2681) — no dest expand / no overdraw

Dest expansion and device-pixel overdraw were the wrong tool for QPainter.

**Intended model**
- Grid dest = exclusive **W×H** content rect (`tile_content_rect`).
- Bitmap may be **(W+1)×(H+1)** with the extra column/row = shared edge.
- `drawImage(exclusive_dest, full_bitmap)` maps the whole source into W×H so
  `SmoothPixmapTransform` filters toward that extra line at the cell edge.
- Adjacent exclusive dests **abut**; they must not overlap in dest space.

Expanding dest by +1 and/or overdrawing fought that: shared dest strips, wrong
paint-order fights, and “disappearing lines” where the next cell ate the previous
exclusive edge.

**2681:** remove dest expand and seam overdraw; ExactTile uses full source →
exclusive dest only.
