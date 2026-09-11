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


## Pixel quality layers (do not mix)

Gallery and filmstrip must always know *which* kind of pixels they hold.
Promoting a higher layer into a lower slot (or the reverse) causes zoom jumps,
stuck full-res tiles, or permanent soft-ladder skip (`hasDecodedPixels()`).

| Layer | Typical long edge | Source | Role |
|-------|-------------------|--------|------|
| **Embedded / EXIF thumb** | often ≤160–320 | JPEG APP1, some codecs | Fastest stand-in; may be wrong colours/aspect; never treat as soft ladder |
| **Fast-path decode** | requested `maxEdge` | `QImageReader::setScaledSize`, `vips_thumbnail` | Shrink-on-decode without full raster; good cold start |
| **Soft ladder** | 128 / 256 / **512** max | thumtoo `get_pixels` / JXL ladder | Durable overview; `PixelKind::SoftPreview`; `setPreviewImage` |
| **Display-sized** | on-screen ladder step (≤2048) | `loadThumbnail` / archive·page shrink-on-decode | Gallery visible tiles when need > soft max — **not** native `ImageLoader::load` |
| **Full decode** | native | `ImageLoader::load` | **Image mode only** |

### Rules

1. **`PixelKind::SoftPreview`** → `ImageItem::setPreviewImage` only. Layout size stays native/probe; never adopt soft dimensions into intrinsic.
2. **`PixelKind::FullSource`** → `setSourceImage`. Sets `hasDecodedPixels()`. Gallery soft scheduler **skips** that path until full pixels are cleared.
3. **Reset Content Appearance** in Gallery must reinstall **soft** (or clear + reschedule), never `ImageLoader::load` into the tile.
4. Filmstrip overrides should follow the same edge budget as filmstrip (`kFilmstripLadderEdge` / prepared thumb), not a native dump from Gallery full decode.
5. Do not ask thumtoo soft ladder for 1024/2048 — those levels are not soft; use full decode when needed.
6. Gallery requests the **on-screen** ladder edge (no soft-max cliff, no native full). Durable soft levels stay ≤512; larger cells use shrink-on-decode. Image mode alone does `ImageLoader::load`.

### Related APIs

| API | Layer |
|-----|--------|
| `ImageLoader::loadThumbnail(path, maxEdge)` | Soft ladder bytes if present, else fast-path / schedule |
| `ImageLoader::load(path)` | Full decode only |
| `ThumtooCache::cachedLadderBytes` / `schedulePixels` | Soft ladder |
| `ImageItem::hasDecodedPixels()` | True only for full source (not soft preview) |
| `ImageItem::clearDecodedPixels()` | Drop both; used before Gallery soft reinstall |
| `GallerySoftState` | Per-path soft/full inflight bookkeeping |

Future thumtoo work: tag ladder payloads vs embedded thumbs explicitly so hosts
never confuse EXIF stand-ins with a completed soft level.


## Open sequence (size-first)

1. **`primeGalleryGeometryFromCache`** — durable `cachedSize` + LQIP only (no ladder encode).
2. **Placeholders + pack** — layout uses known aspects; provisional cells wait for `sizeReady`.
3. **`updateGalleryDecodeWindow`** — soft/full ladder by on-screen edge; size probe runs in parallel (provisional no longer blocks decode).

## Two paths (aligned with thumtoo)

| Path | When | Mechanism |
|------|------|-----------|
| **Soft ladder** | Overview / off-screen; on-screen need ≤ **512** | `request_pixels` / `loadThumbnail` (thumtoo soft max = `kMaxSoftLadderEdge`) |
| **Display-sized** | Visible tile, on-screen need **> 512** | `loadThumbnail` → archive/page shrink-on-decode at that edge |
| **Full decode** | Image mode only | `ImageLoader::load` — native |

Soft ladder is **not** asked for 1024/2048 durable levels. Gallery still
requests those *display* edges: shrink-on-decode to the cell size, not a
native full-page dump.

Display-sized work is **on demand** for visible tiles (bounded by
`kMaxConcurrentGalleryDecodes`), not a permanent full-res cache for every page.

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


## Pitfalls fixed

- Full upgrade must **not** go through `onImageLoaded(LoadAdd)` / pending paths:
  soft completion can `takePending` and drop the full image.
- Soft preview must **not** write intrinsic layout size (avoids zoom/pack jump
  when full pixels arrive at native dimensions).
- `schedulePixels` after page rasterize is clamped to soft max (512), not native.

## Placeholders

Gallery always creates soft placeholders for every session path (no size threshold). Full `LoadAdd` is not used to *create* tiles — it cannot for `//page:` under thumtoo.
