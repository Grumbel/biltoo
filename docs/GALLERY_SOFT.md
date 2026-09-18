<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Gallery pixels: soft ladder + on-demand full decode

## Tiles only (product)

Gallery **tileLodWanted** cells: **durable tiles + LQIP underlay only**.

* **No** PreferCache soft whole-frame underlay
* **No** classic `loadThumbnail` soft plate
* **No** SOFT/HOST samples as underlay (overlay HOST-SAMPLE means a bug if painted)

Blank cells show LQIP (≤96) if present in Store/ImageCache, else empty until
tiles paint. `scheduleGalleryDecode` for the tile band: probe, LQIP install,
`scheduleTilePyramid`, `tickPrimaryTileLod` — then return (no PathRaster soft).

Small cells (`!tileLodWanted`): LQIP only (want capped ≤96).

Warm `ImageCache` LQIP cover → zero work for underlay.

`publishGalleryInterest` uses **LQIP near/spec edges only** — never soft-band 512 (that PreferCache soft was the DEBUG_OVERLAY soft req=512 flood).

### Debug overlay (`BILTOO_DEBUG_OVERLAY`)

| Stamp | Meaning |
|-------|--------|
| **LQIP** | ≤96 host/store stand-in (allowed underlay) |
| **HOST-SAMPLE** | Host whole-frame >96 — **must not** be Gallery underlay |
| **TILE** / **LADDER** (thumtoo) | Durable pyramid cells |


## Authority (host climb)

Gallery soft/display climb is scheduled through **PathRasterService::ensure**
(`scheduleGalleryDecode`). Deliveries land via thumtoo `ladderReady` →
`applyGalleryLadderReady` → `ImageCache` + tiles. `GallerySoftState` still owns
per-path want/have/inflight and the decode-window concurrency budget.

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
6. Gallery requests the **on-screen** ladder edge (no soft-max cliff, no native full). Session soft samples stay ≤512 long edge; larger cells use shrink-on-decode or PreferCache/TileSynth. Image mode alone does `ImageLoader::load`.
7. **Crop / orient / grade** live in `SessionAppearanceStore` by `SessionImageId`. Ladder samples are **host-raw**. Every Gallery install goes through `installDisplayPixels` → `materializeDisplay(host, want)` before attach. SoftPreview **includes** scaled crop. Never paint unoriented host under a cropped layout. Path duplicates: one host in `ImageCache`, per-id materialize on each tile. See [CONTENT_PIPELINE.md](CONTENT_PIPELINE.md) install invariant.

#Cost model (soft vs overview vs tiles): [PERFORMANCE.md](PERFORMANCE.md).

## Related APIs

| API | Layer |
|-----|--------|
| `ImageLoader::loadThumbnail(path, maxEdge)` | Soft ladder bytes if present, else fast-path / schedule |
| `ImageLoader::load(path)` | Full decode only |
| `ThumtooCache::cachedLadderBytes` / `schedulePixels` | Soft ladder |
| `ImageItem::hasDecodedPixels()` | True only for full source (not soft preview) |
| `ImageItem::clearDecodedPixels()` | Drop both; used before Gallery soft reinstall |
| `GallerySoftState` | Prioritization mirror only (want/inflight/blank); climb = PathRasterService SoftDisplay |

Climb scheduling is **PathRasterService** with `ClimbPolicy::SoftDisplay`
([THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md)). `gaveUpWant` is synced
from `isGaveUp` / `wantEdge` — Gallery must not invent PreferCache plateau state.


Distinguish EXIF/embedded stand-ins from completed soft samples so hosts do not
treat placeholders as settled soft.


## Open sequence (size-first)

1. **`primeGalleryGeometryFromCache`** — durable `cachedSize` + LQIP only (no ladder encode).
2. **Placeholders** — one tile per session row; intrinsic size from cache or provisional stand-in.
3. **Size-resolve gate (policy A)** — `startGallerySizeResolveIfNeeded` schedules probes for every path still missing a definitive size (archive/page leaves included). **No pack** until all probes settle (or 45s timeout). Tiles are **hidden** until then. Centre HUD: “Resolving sizes… N / M” (`paintHudPanels` must list the gate in its outer if).
4. **`finishGallerySizeResolve` → `applyLayout(EnterGallery)`** — single authoritative pack with real aspects.
5. **`updateGalleryDecodeWindow`** — soft/display ladder by on-screen edge after geometry is final.

Provisional square stand-ins are layout last-resort only; they must not drive the first Gallery pack for cold archives.

**Parallelism:** size probes are submitted per path via `scheduleProbe` →
`request_size`; throughput is limited by thumtoo’s client, not biltoo’s GUI
thread. Concurrent/batch size probes are deferred past 0.1.0 (see TODO tip 767).

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
| `gaveUpWant` | Highest want finished without ~90% soft delivery — no soft retry of that want |
| `failed` | Permanent hard failure |

## Soft transitions

1. Compute `want` from visibility + zoom.
2. If any item already has **full** decode → done for that path.
4. Else soft: if `have >= min(want, 512)` → idle.
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


## Limits and concurrency

| Limit | Default | Where | Purpose |
|-------|---------|-------|---------|
| `kGalleryLadderEdge` | **512** | `thumtoocache.h` | Durable soft max (`thumtoo` `kMaxSoftLadderEdge`). `schedulePixels` never asks higher. |
| `kFilmstripLadderEdge` | **256** | `thumtoocache.h` | Filmstrip / idle placeholder band. |
| `kLadderEdges` | 128…2048 | `thumtoocache.h` | Display-edge snap steps (`ceilLadderEdge`). |
| `kMaxConcurrentGalleryDecodes` | **4** | `imageview.h` | Soft + display-sized workers on the Qt pool (Gallery). |
| `kMaxIdleGalleryDecodes` | **2** | `imageview.h` | Off-screen soft prefetch per window update. |
| `kGalleryDecodeOverscanPx` | **400** | `imageview.h` | Viewport inflate for “visible” decode. |
| `kMaxConcurrentPixelJobs` | **3** | `thumtoocache.cpp` | Concurrent `thumtoo` `request_pixels` (PDF/archive encode). |
| `kMaxPixelQueue` | **48** | `thumtoocache.cpp` | Pending `schedulePixels` queue depth. |
| `kMaxConcurrentThumbLoads` | **12** | `thumbnailbar.cpp` | Filmstrip pool jobs (separate from thumtoo pixels). |

### Progressive Gallery requests

1. While `have < ~90%` of soft max → request **min(want, 512)** via thumtoo soft ladder.
2. After soft is present, if on-screen `want` is higher → display-sized shrink-on-decode.
3. Never `ImageLoader::load` (native) in Gallery.

Optional overrides (process env, read once at first use):

- `BILTOO_GALLERY_DECODE_CONCURRENCY` — overrides `kMaxConcurrentGalleryDecodes` (1–32).
- `BILTOO_THUMTOO_PIXEL_JOBS` — overrides `kMaxConcurrentPixelJobs` (1–16).


## Interest snapshot (thumtoo)

Gallery `updateGalleryDecodeWindow` publishes:

| Role | Source |
|------|--------|
| **Primary** | Selection anchor (or first selected tile) — FocusFull tile pyramid |
| **Near** | Other visible tiles — FastBatch overview ≤1024 |
| **Speculative** | Off-screen rest — soft band ≤512 |

Host soft path remains `schedulePixels` ≤512 for placeholders. Overview is **not**
scheduled via `scheduleOverviewPixels` when `THUMTOO_API_SET_INTEREST` is defined;
only interest snapshots drive overview/FocusFull work.


## Display surface install policy

Install / climb / async materialize for Gallery tiles is governed by
`DisplaySurface::decide` (see [DISPLAY_SURFACE.md](DISPLAY_SURFACE.md)).
Gallery soft recovery syncs each tile’s bound `SurfaceId`, evaluates the
controller, and runs `ImageView::applyDisplaySurfaceAction`.

Do **not** reintroduce host-vs-shown InstallHostBetter loops: pre-crop host edge
must not be compared to post-crop shown edge to force soft reinstall.


## LQIP climb invariants (host)

These must stay true or Gallery tiles freeze on quick-preview:

1. **`st.have` is shown edge only** — never copy PathRaster/ImageCache have into
   it while the tile still paints LQIP (schedule would think soft is done).
2. **PreferCache give-up does not apply to LQIP** — `have <= 96` must still
   schedule SoftDisplay; only `have > 96` may plateau on `gaveUpWant`.
3. **Soft-band samples attach as SoftPreview** — not FullSource (FullSource sets
   `hasDecodedPixels` and skips soft forever).
4. **Pass1 installs soft while size is provisional** — geometry probes must not
   block ImageCache → tile install.
5. **RasterClimb**: LQIP delivery does not set `softAttempted`; soft-band
   shortfalls do not set `preferGaveUp`.

6. **tileLodWanted underlay pixmap must upgrade** — paint draws QPixmap for
   soft base; only filling when null left LQIP under tiles after soft install.

See tips biltoo-1126 … 1135.
