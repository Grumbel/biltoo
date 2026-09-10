# Gallery soft-thumb algorithm

## Zoom

Ctrl+wheel / toolbar zoom scales the **view transform**. Pack cell size in
scene space does not change. On-screen pixel size of a tile is:

    scene_long_edge * view_scale * devicePixelRatio

That value drives the target ladder step (`want`).

## Two pixel bands

| Band | When | Target long edge |
|------|------|------------------|
| Placeholder | Off-screen / idle, or first paint | <= 512 (`kGalleryLadderEdge`), prefer 256 |
| Inspection | Tile intersects the viewport | `ceilLadder(on-screen px)`, max 2048 |

Rendering always uses the **best installed** soft image. Full native decode is
Image mode only.

## Per-path state (`GallerySoftState`)

| Field | Meaning |
|-------|---------|
| `have` | Long edge of soft pixels on the item |
| `want` | Last computed target from visibility + zoom |
| `inflight` | Edge currently requested (0 = idle); **one** request at a time |
| `gaveUpWant` | Highest `want` finished without ~90% delivery — **no retry** of that want |
| `failed` | Permanent hard failure |

## Transitions

1. Compute `want` from visibility + zoom.
2. If `have >= want` → idle.
3. If `inflight != 0` → wait.
4. If `failed` → stop.
5. If `gaveUpWant >= want` and `have > 0` → stop (cannot grow for this want).
6. Else set `inflight = want`, fetch soft pixels once (`loadThumbnail` / ladder).
7. On completion: install if better. If `got >= want * 0.9`, clear give-up.
   If `got` is only a placeholder but thumtoo is still building `want`
   (`isPixelsInflight` / accepted `schedulePixels`), **keep `inflight`** and
   wait for `ladderReady` — do not set `gaveUpWant` yet. Only give up when the
   build is finished and delivery is still short.

## Concurrency

At most `kMaxConcurrentGalleryDecodes` paths with `inflight != 0`. Visible paths
first; small idle budget for off-screen placeholders.
