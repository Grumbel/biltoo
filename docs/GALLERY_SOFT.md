<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery pixels: soft ladder + on-demand full decode

## Zoom

Ctrl+wheel / toolbar zoom scales the **view transform**. Pack cell size in
scene space does not change. On-screen pixel size of a tile is:

```text
scene_long_edge * view_scale * devicePixelRatio
```

That value drives `want` (via `ceilLadderEdge`).

## Two paths (aligned with thumtoo)

| Path | When | Mechanism |
|------|------|-----------|
| **Soft ladder** | Overview / off-screen placeholder; on-screen need ≤ **512** | `request_pixels` / `loadThumbnail` (thumtoo soft max = `kMaxSoftLadderEdge`) |
| **Full decode** | **Visible** tile and on-screen need **> 512** | `ImageLoader::load` — same path as Image mode |

Soft ladder is **not** asked for 1024/2048. Those requests do not create larger
durable levels in thumtoo; they only returned the existing 256-level and left
the UI stuck.

High resolution in Gallery when zoomed is **on demand** for visible tiles
(bounded by `kMaxConcurrentGalleryDecodes`), not a permanent full-res cache
for every page.

## Per-path state (`GallerySoftState`)

| Field | Meaning |
|-------|---------|
| `have` | Long edge of pixels on the item (soft or full) |
| `want` | Target from visibility + zoom (may exceed 512) |
| `inflight` | Soft edge currently requested (0 = idle) |
| `fullInflight` | Full `ImageLoader::load` in progress |
| `gaveUpWant` | Highest want finished without ~90% soft delivery — no soft retry of that want |
| `failed` | Permanent hard failure |

## Soft transitions

1. Compute `want` from visibility + zoom.
2. If any item already has **full** decode → done for that path.
3. If `want > 512` and visible → schedule **full** decode (`fullInflight`).
4. Else soft: if `have >= min(want, 512)` → idle.
5. If soft `inflight != 0` or `fullInflight` → wait.
6. If `gaveUpWant >= softWant` and `have > 0` → stop soft growth for that band.
7. Else set soft `inflight`, `loadThumbnail` / ladder; on shortfall keep waiting
   for `ladderReady` while thumtoo is still building; otherwise set `gaveUpWant`.

## Concurrency

At most `kMaxConcurrentGalleryDecodes` paths with soft or full work in flight.
Visible paths first; small idle budget for off-screen soft placeholders.

## Constants (`ThumtooCache`)

| Name | Value | Role |
|------|-------|------|
| `kFilmstripLadderEdge` | 256 | Prefer for off-screen / first paint |
| `kGalleryLadderEdge` | 512 | Soft max; matches thumtoo `kMaxSoftLadderEdge` |
| `kImageLadderEdge` | 512 | Historical name; soft cap only |

## Debug

`THUMTOO_DEBUG=1`: soft requests log `soft request path need=… have=…`; full
decode logs `full decode path need=… have=…`.
