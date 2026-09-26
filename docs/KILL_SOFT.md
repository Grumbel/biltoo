<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Kill Soft — tiles everywhere

**Status:** decided 2026-09-26. Soft ladder is **not** a product path.
Phases **A–C** landed (rename, climb band vocabulary, underlay cut).

Related: [GALLERY_SOFT.md](GALLERY_SOFT.md) (Gallery done), [TILE_LOD.md](TILE_LOD.md),
[THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md), thumtoo
`docs/PIXEL_AND_ARCHIVE_POLICY.md`, thumtoo `docs/EMBEDDED_PREVIEW.md`.

## Decision

Remove **Soft** (whole-frame soft ladder / SoftOnly encode / Soft as a display
tier peer of tiles) from biltoo product policy and host climb language.

| Keep | Kill |
|------|------|
| **Tiles** (256² LOD) | Soft ladder as quality system |
| **EMB** (EXIF / PDF `/Thumb`) underlay | Soft PreferCache encode for UI |
| **LQIP** (ThumbHash/Handsum) underlay | SoftDisplay as “soft then prefer” story |
| **TileSynth** (`get_pixels_from_tiles`) for CLI/export/one-QImage | Durable soft levels in Store (already gone schema ≥100) |
| Optional one-shot ephemeral shrink when Store empty | Soft band competing with `tileLodWanted` |

### Why

1. Soft does not share math with tile `scale`; it confuses paint, climb, and debug.
2. Soft is not faster than tiles when both need the real pixels; EMB covers free first paint.
3. Cold open should emit **coarse tiles first**, not a parallel soft pipeline.
4. Gallery already removed soft underlay; Image/Workspace should match.

## Target pixel stack

```text
Underlay:  EMB → LQIP → alive placeholder
Display:   tiles (+ coarser parent fallback)
Whole-frame API (non-UI / export): TileSynth from durable tiles
  optional: one-shot ephemeral decode if nothing in Store (not a soft ladder)
```

## Climb policy rename

| Old name | New name | Meaning |
|----------|----------|---------|
| `ClimbPolicy::SoftDisplay` | `ClimbPolicy::TileDisplay` | TileSynth / pyramid / overview band; **never** Full native |
| `ClimbPolicy::EscalateToFull` | unchanged | May schedule Full when need exceeds overview |

`PathRasterService` must not call SoftOnly. PreferCache / `scheduleDisplayPixels`
may assemble via **TileSynth** when tiles exist; on miss schedule **tile pyramid**,
not soft ladder encode.

## Phases

### Phase A — policy + naming (**done**)

- This document; TODO handoff.
- Rename `SoftDisplay` → `TileDisplay` in climb API and call sites.
- Comments: `scheduleSoft` in the plan still means “first ≤512 band via
  TileSynth or pyramid” (already wired that way); SoftOnly encode is not used.

### Phase B — climb SM simplification (**done**)

- Plan `scheduleSoft` → **`scheduleBand`** (TileSynth/pyramid first band).
- `softAttempted` / `softQueued` → **`bandAttempted` / `bandQueued`**.
- Tests updated; TileDisplay still never schedules Full.

### Phase C — Image / Workspace underlay (**done**)

- Under `tileLodWanted`, paint underlay = **EMB/LQIP only** (≤ `kEmbeddedUnderlayMaxEdge`);
  soft/HOST samples are not drawn; placeholder if none.
- Nav-hot stays cache-only (already: no soft generate).

### Phase D — thumtoo (optional, separate tip)

- SoftOnly remains for external CLI hosts if needed; biltoo does not call it.
- PreferCache miss path: prefer coarse tile build over soft ladder encode when
  the consumer is tile-capable (already the long-term PIXEL_AND_ARCHIVE policy).

## Non-goals

- Removing TileSynth.
- Changing tile schema.
- Requiring LQIP generation on size probe (still forbidden).

## Success

1. No product docs describe Soft as a display tier beside tiles.
2. Image/Gallery/Workspace underlay vocabulary is EMB/LQIP/placeholder + tiles.
3. `BILTOO_TILE_DEBUG` / climb logs do not say SoftOnly for biltoo paths.
4. Cold open improves via coarse-first tiles + EMB, not soft ladder.
