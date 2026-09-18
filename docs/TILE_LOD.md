<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile level-of-detail (LOD) — host display path

Normative rules for **viewport-driven grid tiles** in biltoo (and any host that
shares the same core). thumtoo remains the durable tile store; this document
defines the **host-side planner, RAM cache, session, and draw plan**.

Related: [thumtoo TILES.md](../../thumtoo/TILES.md) (encode model), Galapix
`ImageTileCache` (historical problem statement — **not** a port target).

## Goals

1. Deep zoom and large on-screen regions use **256² grid tiles**, not whole-frame
   soft / PreferCache samples.
2. **Logic is Qt-free** and unit-testable (`tilelod` core).
3. Coarser tiles are **temporary stand-ins**; the exact target scale is always
   requested when visible.
4. **LQIP** (and optionally a single soft underlay) is the **base placeholder**
   only until the first useful tile arrives for a cell; after that, paint uses
   tiles (exact or coarser parent). LQIP is retrieved with size when cached and
   may remain attached to the image object, but it is not the LOD system.

## Non-goals (first cut)

- Changing thumtoo tile schema or encode path.
- Process-wide shared tile RAM **across apps** (host process retains paths; see
  global path retention).
- Animating pop-in inside the core (host may fade).
- Porting Galapix `Surface` / global static budgets / job stack.

## Coordinate model (must match thumtoo)

| Symbol | Meaning |
|--------|---------|
| `kTileSize` | **256** |
| `scale` | **0** = full resolution; each **+1** halves width and height |
| `(x, y)` | Tile indices from the top-left of that scale’s image |
| Edge tiles | May be smaller than 256×256 in pixel payload |

Dimension at scale (same as thumtoo `dim_at_tile_scale`): successive
**integer floor-half**, not `ceil(n / 2^scale)`:

```text
dim(n, 0) = n
dim(n, s+1) = floor(dim(n, s) / 2)
```

Grid size at scale `s` for content size `(W, H)`:

```text
tw(s) = ceil(dim(W, s) / 256)
th(s) = ceil(dim(H, s) / 256)
```

`max_scale` = smallest `s ≥ 0` such that `tw(s) == 1 && th(s) == 1`
(or a configured cap). `min_scale` is usually `0` for rasters; PDF may use
negative scales later — core accepts any integer scale range the source reports.

Content-space rectangle covered by tile `(s, x, y)`:

```text
# At scale s the full image is dim(W,s) × dim(H,s) pixels.
# Each tile is 256×256 in that space (edge tiles smaller).
# Map back to content (scale-0) space by multiplying by 2^s:

left   = x * 256 * 2^s
top    = y * 256 * 2^s
right  = min(W, (x+1) * 256 * 2^s)
bottom = min(H, (y+1) * 256 * 2^s)
```

Parent of `(s, x, y)` at coarser scale `s+k` (`k ≥ 1`):

```text
parent = (s+k, floor(x / 2^k), floor(y / 2^k))
```

## Module layout

```text
src/tilelod/          # Qt-free core
  tile_types.hpp      # TileKey, Rect, constants
  lod_math.hpp/.cpp   # dim, grid, content rect, parent
  lod_planner.hpp/.cpp
  draw_plan.hpp/.cpp
  tile_memory_cache.hpp/.cpp
  tile_source.hpp     # abstract async fetch
  tile_session.hpp/.cpp

# Later (thin Qt / product boundary — not in first tip):
#   ThumtooTileSource, TilePainter, registry on host
```

Tests link **only** `tilelod` (+ fake source). No Qt, no thumtoo in unit tests.

## Pure planner

**Input:** content `W×H`, viewport rectangle in **content space**, desired
**device pixels per content pixel** (from the view transform).

**Output:**

- `target_scale` — finest scale whose tile pixel density is ≥ need
  (clamped to `[min_scale, max_scale]`).
- `visible_keys` — all `(target_scale, x, y)` whose content rect intersects the
  viewport (optional margin in content pixels).

No I/O. Choosing target scale:

```text
# Need roughly (device_px / content_px) samples per content unit.
# At scale s, one content unit maps to 1/2^s tile-space pixels.
# We want 2^{-s} ≳ device_per_content  (tile pixels per content ≥ screen need)
# ⇒ s ≲ -log2(device_per_content)
# Prefer the finest scale that still meets density, clamped.

need = device_pixels_per_content_pixel
if need <= 0: treat as very zoomed out
ideal_s = round toward coarser of floor( -log2(need) )   # need=1 → scale 0
target_scale = clamp(ideal_s, min_scale, max_scale)
```

Exact formula is implemented in `LodPlanner` and locked by tests.

## Fallback (draw only — do not request parents)

For each visible key `K = (s, x, y)` when building a **DrawPlan**:

1. If cache has **exact** `K` → use it (full UV of that tile payload).
2. Else walk coarser scales `s+1, s+2, …` and take the **finest** parent cell
   that is already **Succeeded** in the RAM cache; compute UV sub-rect of that
   parent covering `K`’s content region.
3. Else if session has an **LQIP / underlay** still active for this image and
   **no** tile (exact or parent) has succeeded for this cell yet → host may
   paint LQIP for that region.
4. Else empty → host placeholder.

**Do not** mark coarser parents as “needed” solely for fallback. Request **only**
exact visible cells at `target_scale` (plus optional one-shot overview prime at
`max_scale` if the product wants an early whole-image stand-in beyond LQIP).

Lesson from Galapix: mixing “mark ancestors needed” with cancel produced stuck
`REQUESTED` cells and purple voids.

### LQIP policy (product)

- LQIP is fetched with size when already cached; it **stays attached** to the
  image identity for the lifetime of the open item if desired.
- Once **any** tile for a visible cell has arrived (exact or coarser used for
  paint), that cell’s draw entry prefers the tile; LQIP is no longer the active
  fill for that region.
- Session may expose `has_any_tile()` so the host can drop global LQIP underlay
  after the first successful tile for the current viewport generation.

## TileSession (state machine)

Per open image (URI / content id):

| Host call | Effect |
|-----------|--------|
| `set_content_size(W,H)` | Computes `max_scale`, grid dims; required before viewport |
| `set_lqip(...)` | Optional base underlay (opaque blob / bitmap handle) |
| `set_viewport(vp, device_per_content)` | Recomputes `target_scale` + visible set |
| `pump(completions)` | Integrate arrivals; clear in-flight; bump generation as needed |
| `issue_requests(budget)` | Start up to N missing **exact** keys (center-first priority) |
| `draw_plan()` | List of `{dst_content_rect, src_key, src_uv, kind}` |
| `cancel_obsolete()` | Best-effort cancel in-flight keys no longer visible |
| `trim_cache(...)` | Optional LRU / far-scale drop |

Completions may arrive on a worker thread; `pump` is called from the host frame
tick and applies them under session ownership.

**Generation:** each `set_viewport` that changes the visible set may bump a
generation counter; late completions for obsolete keys are ignored.

## TileSource (async interface)

```text
request(keys[], on_each(key, optional<TileBitmap>))
cancel(keys[])   // best-effort
max_scale / min_scale / content size from probe
```

Production: thumtoo `get_tile` / `request_tiles`. Tests: fake solid-color or
checkerboard tiles keyed by `(s,x,y)`.

Decode JPEG → RGBA happens in the source/worker path (or stays compressed for
the painter); the core stores opaque `TileBitmap` (width, height, pixels or
encoded bytes + codec).

## Integration phases (biltoo)

| Phase | Work |
|-------|------|
| **A** | `tilelod` + tests (this tip family) — no product UI change |
| **B** | Image-mode deep zoom uses `TileSession` + Qt painter |
| **C** | PreferCache / whole-frame climb no longer authority for zoomed display |
| **D** | Workspace items; Gallery large cells if needed |
| **E** | Delete dead whole-frame zoom path; soft ladder remains for thumbs |

Do not delete thumtoo soft APIs in early tips; stop **calling** them for deep
zoom first.

## Testing matrix (no Qt)

| Test | Asserts |
|------|---------|
| Planner 1:1 | scale 0, correct cell count for viewport |
| Planner zoomed out | higher scale, fewer cells |
| Fallback parent | missing `(0,0,0)` uses parent with correct UV |
| Budget | only N requests leave `issue_requests` |
| Cancel / generation | after viewport move, obsolete completions ignored |
| Full coverage | entire image in view → all cells at target scale |
| Edge tiles | non-multiple of 256 dimensions |

## Galapix checklist (regressions to avoid)

- Do not enqueue from the draw / fallback path.
- Do not mark parents needed only as stand-ins.
- Per-frame request budget; prioritize center / exact scale.
- Cancel obsolete in-flight on pan/zoom; ignore stale completions.
- Overview / LQIP is underlay, not mixed identity with high-quality tiles.
- Keep succeeded coarser tiles for fallback when cleaning finer scales.


## Phase B status (biltoo-1026)

- `ThumtooTileSource` + `ThumtooCache::requestTiles` / `getTile`
- `tile_painter` + `TileLodController`
- **ImageItem** (interactive): when on-screen long edge exceeds soft max (~512),
  updates viewport, issues budgeted tile requests, paints exact/coarser tiles
  over the existing soft/LQIP sample. LQIP/soft remains the base underlay until
  cells have tiles.
- PreferCache whole-frame climb is still present for other paths (Phase C).


## Phase B+ status (biltoo-1027)

- Tile **requests** are issued from `ImageView::tickPrimaryTileLod` (zoom/resize
  climb + 33ms timer while `tileLodWanted`), not from `ImageItem::paint`.
- Paint only `prepareTileLod` + `paint` (draw plan).
- When `tileLodWanted()` (on-screen long edge past soft ~512), Image-mode
  **skips PreferCache whole-frame climb**; soft/LQIP stays the underlay.


## biltoo-1028

- Image mode items are **not** interactive; `tileLodWanted` must not gate on chrome.
- Gallery cells: LQIP underlay + tiles when on-screen edge > 32 px (soft PreferCache removed).
- Workspace: same tick path as Image for selection / up to 8 items.
- Crossing into tile band cancels PathRaster PreferCache for that path.


## biltoo-1029 — shared path cache

- **Shared:** `ThumtooTileSource` + `TileMemoryCache` per path (`TileLodRegistry`).
- **Per item:** `TileSession` (viewport, request budget, generation).
- Workspace duplicates of the same file reuse Succeeded tiles in RAM.
- Cancel obsolete never drops Succeeded entries from a shared cache.


## biltoo-1030

- Device density includes **devicePixelRatioF** (logical zoom × DPR).
- PreferCache cancel is **edge-triggered** (once per path in tile band).


## biltoo-1031 — scale hold

During continuous zoom, **adjacent** scale steps are held ~150ms before
re-planning the visible grid (avoids enqueueing intermediate pyramids every
wheel notch). Jumps of more than one scale commit immediately.

Manual checks: [TILE_LOD_RUNTIME.md](TILE_LOD_RUNTIME.md).


## Content orient / crop

thumtoo grid tiles are keyed in **source** pixel space. Item layout is
**display** space after flips → quarter-turns → crop
([CONTENT_COORDINATES.md](CONTENT_COORDINATES.md)).

`ContentXform::mapDisplayRectToSource` / `mapSourceRectToDisplay` convert the
viewport and draw destinations. Free-rotated crop uses the materializeDisplay centre/rotate window; maps return
the AABB of transformed corners for viewport request and draw destinations.


## PreferCache vs tiles (biltoo-1035)

When `tileLodWanted()` is true (on-screen need past soft **and** durable
tiles exist), hosts must not schedule PreferCache/Full whole-frame climbs
for that path:

- Gallery decode window skips those cells
- `requestEscalateClimb` returns early for Image-mode tile-band items
- Workspace quality climb already skipped; PathRaster cancel-once on enter


## Coverage + heartbeat (biltoo-1036 / 1037)

`TileSession::coverage()` reports exact Succeeded vs visible keys.

| State | Timer |
|-------|--------|
| Wanted + incomplete | 33ms pump |
| Wanted + fully covered | 250ms heartbeat (pan discovers new cells) |
| Not wanted | stopped |


## Cache budget (biltoo-1038 / 1040)

Shared path `TileMemoryCache` keeps at most ~**128 MiB** of Succeeded tile
payloads by default. After each `pump`, **visible keys and their coarser
parents** are protected (draw-plan stand-ins); older Succeeded tiles are
evicted by `last_used`. InFlight entries are never dropped by the budget trim.


## Global path retention (biltoo-1212)

`TileLodRegistry` is the process-wide path-keyed RAM home for tiles:

| Layer | Scope | Lifetime |
|-------|--------|----------|
| **thumtoo Store** | Durable on disk | Independent of host |
| **`SharedPathTiles` + `TileMemoryCache`** | Per path, process-wide | Survives last controller `release` until global LRU eviction |
| **`TileSession`** | Per `ImageItem` (viewport, generation, in-flight) | Item path change / destroy |
| **`TileLodController`** | Glue on the item | `setPath` / suppress |

**Rules:**

1. **Pixels live in the global path cache**, not on the `ImageItem`. Switching
   image or mode must not drop Succeeded tiles for a path still inside the budget.
2. **`TileSession` stays per view surface** — cheap to create/destroy on ←/→.
3. **`release` drops interest only** — refcount → 0 does **not** erase the entry
   when the cache still holds Succeeded (or InFlight) tiles. Empty idle shells
   are dropped immediately.
4. **Global budget** (~**384 MiB** default of Succeeded payload across all paths):
   when over budget, oldest **zero-ref** path entries are dropped whole. Active
   paths rely on per-session `trim_to_budget` (128 MiB).
5. **Paint identity by path** — draw only from `cache[item->path()]`; re-acquire
   after A→B→A rebinds the same retained entry.
6. **`invalidate(path)`** force-drops an entry (file replaced / explicit wipe).

Nav-hot / suppress remains optional request-budget polish, not the mechanism that
keeps identity correct across path switches.


## Scale hold (biltoo-1095)

Adjacent-step hold applies only when **zooming in** (finer scale). **Zoom-out**
commits the coarser target immediately so the request grid does not stay on
high-res cells while the content viewport expands.


## Zoom-out drop (biltoo-1039 / 1041)

When the held target scale **increases** (zoom out) on a **private** session
cache, Succeeded tiles with `scale < target` are dropped. **Shared** path
caches skip this (Workspace duplicates must not lose fine tiles another view
still needs); they rely on the 128 MiB budget trim + parent protect instead.

Debug: `BILTOO_TILE_DEBUG=1` prints covered/active per tick.


## Failed tiles (biltoo-1042)

A tile that fails for the current viewport generation is not re-requested on
every pump tick. Changing the viewport (generation++) allows a retry. Prevents
thumtoo hammering when a cell is missing from the pyramid.


## Session lifetime (biltoo-1043)

Source callbacks capture a `shared_ptr<CompletionInbox>`, not `TileSession*`.
Destroying the session marks the inbox dead, cancels in-flight keys, and
clears pending completions so late worker replies are no-ops.


## Parent prefetch (biltoo-1044)

After exact visible keys are queued, remaining request budget fetches one
coarser parent per missing exact cell so `CoarserTile` stand-ins appear while
fine tiles load. `cancel_obsolete` keeps those parent keys in-flight.


## Implementation status (1044)

| Area | Status |
|------|--------|
| Qt-free planner / session / draw plan | Done |
| Thumtoo source + Image / Workspace / Gallery | Done |
| Shared path RAM cache + 128 MiB budget | Done |
| Global path retention + 384 MiB LRU (1212) | Done |
| Parent protect + parent prefetch | Done |
| HiDPI, scale hold, coverage heartbeat | Done |
| ContentXform axis-aligned map | Done |
| PreferCache skipped in tile band | Done |
| Session lifetime / failed no-spam | Done |
| Free-rotated crop UV | Done (maps + paint transform) |
| Color-graded content | Done (grade on paint resolve) |
| Crop draft suppresses tiles | Done (1053) |
| Gallery on-screen cell threshold | Done (1054) |
| Failed no-spam (plan-stable generation) | Done (1055) |
| Tick update only on plan/completion | Done (1056) |
| Leave-band stops tile requests | Done (1057) |
| Tile fetch without QImage round-trip | Done (1058) |
| Identity tile QImage paint cache | Done (1059) |
| Tile source epoch not per-request | Done (1060) |
| tileLodWanted density-only (no durable gate) | Done (1097) |
| hasDurableTiles positive memo | Done (1062) |
| durableTilesReady wakes tile tick | Done (1063) |
| set_content_size idempotent | Done (1064) |
| Pan restarts tile tick | Done (1065) |
| Scrollbar restarts tile tick | Done (1066) |
| Host prepare-loop regression test | Done (1067) |
| Session min_scale from durable coverage | Done (1068) |
| Gallery tick prioritizes visible cells | Done (1069) |
| Workspace tick prioritizes + bounds | Done (1070) |
| min_scale clamp pure test | Done (1071) |
| Parent UV + underlay stand-in | Done (1072) |
| Zoom path GUI-thread perf | Done (1073) |
| Debug overlay LADDER/TILE/HOST | Done (1074) |
| Zoom-out plan refresh (coarse scale) | Done (1075) |
| Background load GUI relief | Done (1076) |
| Manual pyramid QA | See TILE_LOD_RUNTIME.md |



## Free-rot paint (biltoo-1047)

Viewport requests use the free-rot AABB in source space. **Paint** applies the
same centre/`rotate(-θ)` transform as `materializeDisplay` and draws tile
rects in **oriented** coordinates so cells are not axis-aligned-squashed into
the AABB.


## Destroy clears InFlight (biltoo-1048)

When a session is destroyed with outstanding requests, InFlight entries are
removed from the shared path cache so another ImageItem of the same path does
not stall forever waiting for a completion that will never be pumped.


## Background load GUI relief (biltoo-1076)

While soft/tiles complete, the GUI thread was still busy:

| Issue | Mitigation |
|-------|------------|
| `prepareTileLodPlan` every paint while cells stream | Skip when dpc + visSource unchanged |
| `update()` per tick completion | Coalesce to one queued `update()` per event-loop turn |
| Tile pump at 33ms while incomplete | 50ms (~20Hz) fill; 250ms when covered |
| `onLadderReady` → always `tickPrimaryTileLod` | Only if an item for that path `tileLodWanted()` |
| Qt global pool uses all cores | Cap at `idealThreadCount - 1` (leave GUI headroom) |

Decode remains off-GUI. Remaining cost is paint of visible cells + occasional plan.


## Zoom-out plan refresh (biltoo-1075)

After 1073, paint used the last tick plan. Zooming out kept **scale-0** cells on
screen until the debounced tick, looking like a carpet of tiny full-res tiles.

Paint now calls `prepareTileLodPlan()` (viewport + plan only, no
`issue_requests`). Tick still owns fetches. Margin is ~64 **device** px in
content space (`64/dpc`). Pure test: dpc=0.05 → target scale ≥ 4.


## Debug overlay kinds (biltoo-1074)

With `BILTOO_DEBUG_OVERLAY=1` / `THUMTOO_DEBUG_OVERLAY=1`:

| Stamp / outline | Meaning |
|-----------------|--------|
| **LADDER** / **SOFT** (thumtoo, yellow on pixels) | Soft overview sample ≤512, stretched as underlay |
| **TILE** `scale=N (1:2^N)` (thumtoo) | Grid cell; scale 0 = full-res |
| **HOST** (biltoo cyan plate, BR) | ImageCache PreferCache / host sample (one plate, not a grid) |
| **TILE** summary plate (paint) | Target scale, vis/exact/parent/hole, flight, LOADING/WAITING |
| **Yellow wash** | ExactTile — target-scale tile cache |
| **Orange wash** | CoarserTile — parent scale cache stand-in |
| **Cyan wash** | Underlay hole — soft/ladder underlay |

Center without tile outlines is usually HOST/PreferCache underlay where the
tile band is inactive (Gallery soft-covered) or tiles have not covered yet.


## Zoom path performance (biltoo-1073)

### Analysis (what was slow)

JPEG/tile **decode is not on the GUI thread** — `ThumtooCache::requestTiles` →
thumtoo worker → completion inbox → `pump()` on tick. The sluggish zoom feel
came from **synchronous GUI work every wheel notch / every paint**:

| Hot path | Cost | Notes |
|----------|------|--------|
| Image/Workspace **wheel** → `maybeClimb` / `ensureWorkspace` → `tickPrimaryTileLod` | High | Per notch: up to 8× `prepareTileLod` → `set_viewport` → `cancel_obsolete` → `issue_requests` |
| **Paint** → `prepareTileLod` | High | Same plan work again on every repaint while zooming |
| `cancel_obsolete` when plan unchanged | Medium | Walked all InFlight keys every call |
| `tile_bitmap_to_qimage` first paint of a cell | Medium | One-time 256²×4 copy; then graded/identity QImage cache |
| `SmoothPixmapTransform` × N tiles | Medium | Expected for non-integer zoom |
| `hasDurableTiles` | Low after 1062 | Positive memo; first hit may touch SQLite |

Gallery Ctrl+wheel was already better: `scheduleGalleryDecodeWindowRefresh(120)`.

### Changes

1. **Debounce zoom climb/tick** (`scheduleTileLodAfterInteraction(50)`): continuous
   wheel/toolbar zoom coalesces to one climb+tick after settle. Pan still ticks
   immediately (1065/1066).
2. **Paint does not `prepareTileLod`**: plan is owned by the tick path; paint
   draws the last plan over soft.
3. **`cancel_obsolete` only when `plan_changed`**: stable viewport no longer
   walks InFlight.

### Still on GUI (acceptable / later)

- `drawImage` of visible cells with smooth transform (must be paint-thread).
- `statusChanged` / chrome on zoom.
- First-time QImage conversion per cell (cached after).


## Parent UV stand-in (biltoo-1072)

Two host/paint bugs made missing fine tiles look like **repeated** low-res content:

1. **Per-cell soft underlay** — non-free-rot paint passed the full soft image as
   `lqip`, so every `Underlay` command stretched the whole soft into that cell.
   Soft is already painted full-frame; leave `lqip` empty (same as free-rot).
2. **Parent UV** — `parent_uv_for_child` now maps fine/parent **content rects**
   into parent pixel space so edge tiles (partial payloads) get the correct
   sub-region, not a uniform span subdivision that drifts.


## min_scale clamp test (biltoo-1071)

Pure test: `set_content_size(..., min_scale=2)` with high device density still
targets scale ≥ 2 and never requests finer keys (guards 1068 at the session layer).


## Workspace tick priority (biltoo-1070)

Workspace tile tick uses selection when non-empty, else all canvas items.
Both paths are **bounded to 8** and sorted: `tileLodWanted` first, then in-view,
then on-screen need edge. Large multi-select no longer ticks every selected item.


## Gallery tick priority (biltoo-1069)

Gallery tile tick is bounded to 8 items. Selection was list order, so late cells
past the soft threshold could starve. Candidates are sorted **in-view first**,
then by on-screen long edge (cell × view scale × DPR).


## Session min_scale (biltoo-1068)

`ThumtooCache::durableTileMinScale` records the finest durable scale when
discovering a pyramid (0 if origin exists, else coverage `min_scale`). Host
passes it to `setContentSize` so the planner never targets finer than the
Store can supply (avoids systematic Failed cells for partial pyramids).


## Host prepare-loop test (biltoo-1067)

Pure test simulates `prepareTileLod` (set_content_size + set_viewport every
frame). Generation and visible keys stay stable; failed cells are not
re-requested while the plan is unchanged (guards 1055 + 1064 together).


## Scrollbar restarts tile tick (biltoo-1066)

Image/Workspace scrollbar `valueChanged` calls `tickPrimaryTileLod` when not
already hand-panning (Gallery already refreshes via decode window). Complements
1065 for scrollbar-driven viewport moves after coverage stop.

Runtime checklist (`TILE_LOD_RUNTIME.md`) refreshed for durable-tiles gate, pan,
crop suppress, failed no-spam, and mid-session pyramid.


## Pan restarts tile tick (biltoo-1065)

The tile timer stops once the visible set is fully covered. Hand pan changes
the viewport without a zoom/climb event, so `tickPrimaryTileLod` was never
restarted and new cells waited until the next wheel/resize.

`tryMouseMovePan` / `tryMouseReleasePan` call `tickPrimaryTileLod` so deep-zoom
pan keeps filling the grid.


## set_content_size idempotent (biltoo-1064)

`prepareTileLod` calls `setContentSize` every paint/tick. `set_content_size`
always cleared `visible_keys` and bumped generation, so `set_viewport` always
saw a plan change — failed no-spam (1055) never held on the host path.

No-op when width/height/min_scale are unchanged.


## durableTilesReady (biltoo-1063)

First process-wide positive `hasDurableTiles` emits `Bridge::durableTilesReady`.
ImageView starts `tickPrimaryTileLod` so a mid-session pyramid (or FocusFull)
can enter the tile band while already deep-zoomed (timer was stopped when
wanted was false). `onLadderReady` also ticks as a second path.


## Durable tiles memo (biltoo-1062)

`hasDurableTiles` is on the paint/tick hot path via `tileLodWanted`. Positive
hits are memoized process-wide (`g_durableTilesYes`) so SQLite `has_tile` is not
repeated every frame. Negatives are not cached so a mid-session pyramid build
can still enable the tile band.


## Durable tiles gate (biltoo-1061)

`tileLodWanted()` is density-based (on-screen long edge past soft). Durable
tiles are optional (fast Store hits); without them interactive `request_tiles`
encodes on miss. Crossing into the tile band cancels PreferCache; soft stays
would stick with no Prefer climb. PreferCache/Full keep authority until durable
tiles exist (after `thumtoo-prepare --tiles` or FocusFull pyramid build).


## Tile source epoch (biltoo-1060)

`ThumtooTileSource::request` used to `++m_batch_id` on every call. A second
`issue_requests` batch then dropped all completions from the first (`batch !=
m_batch_id`), leaving cells stuck **InFlight** in the shared RAM cache.

Epoch advances only on `set_uri` / `cancel_all`. Concurrent request batches of
the same path complete normally.


## Identity tile QImage cache (biltoo-1059)

`resolveGradedTile` always caches the `TileBitmap` → `QImage` conversion,
including identity grade. Previously identity re-copied every rgba8 cell on
every paint frame. Grade signature still invalidates the cache on adjust change.


## Tile fetch bitmap path (biltoo-1058)

`ThumtooCache::requestTiles` delivers `tilelod::TileBitmap` (rgba8 after one
decode) instead of `QImage`. The registry `makeFetch` path no longer converts
TileBitmap → QImage → TileBitmap for the shared RAM cache.


## Leave tile band (biltoo-1057)

When `tileLodWanted()` becomes false (zoom out below soft max), `tickTileLod`
must not continue pumping the previous deep-zoom session. Previously
`prepareTileLod` returned early but `tick()` still issued requests for the
stale viewport.

Gate: `tickTileLod` returns immediately when not wanted; `tickPrimaryTileLod`
only calls it for wanted items. Session/RAM cache are kept for a later re-entry.


## Tick update throttle (biltoo-1056)

`tickTileLod` used to call `update()` whenever any tile had succeeded, including
the 250 ms covered heartbeat with no new completions. That forced continuous
repaints (and defeated Gallery `DeviceCoordinateCache`).

Repaint now only when:

- completions were applied (`applied > 0`), or
- plan generation changed (pan/zoom needs parent UV stand-ins)

`setPath` clears suppress + last-update generation.


## Failed no-spam generation (biltoo-1055)

`TileSession::set_viewport` used to `++m_generation` on every call. The host
calls it every paint/tick with a stable viewport, so `Failed.generation` never
matched `m_generation` and failed cells were re-requested every 33 ms
(defeating biltoo-1042).

Generation now advances only when the **plan** changes (target scale or
visible key set). Identical viewport re-sets keep the generation; a real pan/
zoom that changes keys or scale allows retry.


## Gallery cell threshold (biltoo-1054 / 1080)

`tileLodWanted` for packed Gallery cells uses the **on-screen cell** long edge:

- `galleryCellSize` is the scene footprint (item scale already applied at pack).
- Map scene → device with the **view transform only** × DPR.
- Do **not** multiply by `tileDevicePerContent()` (view×item): that under-counted
  by ~itemScale and blocked Ctrl+wheel inspection tiles.

**1098:** Gallery threshold is **256** (one tile side). Soft no longer blocks
tiles when a host sample “covers” the cell — coarse tiles own that band.
Historical 1080 note: soft-cover early-out removed.
tile band — overview with soft 512 must not spawn dense HOLE grids / tile
sessions. Ctrl+wheel inspect still crosses the threshold.

Image / Workspace still use content long edge × `tileDevicePerContent()` and
the 512×1.05 soft-max rule.

## Tiles own plate (biltoo-1101)

Once `tileLodWanted` and any cell has landed, paint does **not** stretch a
soft/HOST underlay under the grid. Gallery does not SoftOnly when tiles want
the cell. Overlay labels: LQIP ≤96, SOFT ≤512, HOST larger.


## LQIP then tiles (biltoo-1099)

Image mode underlay is **cache-only LQIP / ImageCache** — no SoftOnly encode.
Tiles are issued for the visible set plus ~**one tile side** of screen margin
(256 device px). PreferCache/Full are not started in parallel when tiles own the view.


## Tiles replace soft ≤512 (biltoo-1098)

`shouldUseTiles` / Gallery threshold: on-screen long edge **> 256** (one tile side).
Image-mode classic decode and quality climb skip SoftOnly/PreferCache when
`tileLodWanted` — coarse tiles cover the old soft band.


## Tile band without durable pyramid (biltoo-1097)

`tileLodWanted` no longer requires `hasDurableTiles`. PreferCache/Full whole-frame
climbs (often →2048) were redundant with interactive tiles once on-screen need
passed the soft band. Soft remains the underlay until cells arrive.


## hasDurableTiles memo (biltoo-1084)

Positive paths stay memoized. Misses use a **2.5s negative TTL** so Gallery paint
does not hit Store `has_tile` every frame. Cleared on discover / scheduleTilePyramid.


## Global issue budget (biltoo-1083)

`tickPrimaryTileLod(N)` splits **N** across the (≤8) priority targets for that
tick. Earlier code gave each target the full N (up to 8×N concurrent requests).


## Graded cache keys / issue budget (biltoo-1092)

Cache keys are packed `quint64` (scale,x,y). Timer issue budget: **12** in Image
mode, **6** in Gallery/Workspace (split across ≤8 targets).


## Graded tile QImage cache (biltoo-1091)

Paint resolves Succeeded cells to QImage via a cost-bounded QCache (~96 MiB,
LRU) instead of clearing the whole hash at 256 entries.


## Draw plan cache validity (biltoo-1090)

`set_has_lqip` only dirties the cached plan when the flag changes. prepare no
longer calls it on the stable dpc/vis early-out path.


## Soft base under tiles (biltoo-1089)

When the tile plan fully covers the viewport (`tileLodViewportCovered`), the
soft/PreferCache base is not painted. Soft path uses `drawPixmap` (no
`pixmap().toImage()` copy) while tiles are still filling.


## Draw plan cache (biltoo-1088)

`TileSession::draw_plan()` caches the last plan until viewport plan change or
tile completions are pumped.


## Tile paint transform (biltoo-1087)

`SmoothPixmapTransform` is off for ExactTile-only plans when device zoom of the
target scale is near an integer ≥1 (nearest-neighbour, cheaper). Parent stand-ins
still use smooth filtering.


## Underlay plan commands (biltoo-1086)

Host paints soft as a continuous base. Draw plans omit Underlay/Empty hole cells
unless `has_lqip` (legacy). Plan size tracks exact+parent only under normal zoom.


## Scale-0 gate (biltoo-1085)

Exact cells at **scale 0** are not issued until the path cache has a Succeeded
tile at **scale ≥ 1** (unless the pyramid is single-level). Keeps the first wave
on DCT-shrink parents.


## Cold issue order (biltoo-1082)

`TileSession::issue_requests`: when the path cache has **no** Succeeded tile yet,
enqueue **parent(+1), parent(+2), then exact**. Warm paths keep exact-first.

Matches thumtoo cost model: JPEG `scale>0` → DCT shrink; `scale==0` → full decode.


## Debug overlay readability (biltoo-1080 / 1081)

- **HOST** (ImageCache): single cyan plate, bottom-right — not a repeated grid.
- **Tile plan** (`BILTOO_DEBUG_OVERLAY` / `BILTOO_TILE_DEBUG`): summary plate with
  target scale, vis/exact/parent/hole counts, `ok`/`flight`, and
  `LOADING…` / `WAITING` / `COMPLETE`; short per-cell tags only when ≳40 device px.
- **Washes (1081):** semi-transparent full-cell fills (image still visible):
  - **Yellow** — ExactTile (target-scale cache)
  - **Orange** — CoarserTile / parent cache stand-in
  - **Cyan** — Underlay hole (soft / ladder showing through)


## Crop draft suppress (biltoo-1053)

While the crop-draft sample is frozen (`m_cropDraftSampleFrozen`), PreferCache
climb is already blocked via `isCropDraftLockedPath`. Tile LOD now matches:

- `ImageItem::setTileLodSuppressed(true)` on freeze → `tileLodWanted()` false,
  private `TileLodController` dropped (shared path RAM cache kept).
- `tickPrimaryTileLod` skips locked/suppressed items.
- Clear suppress on `clearCropModeState` and failed crop prepare.

Prevents tile requests/paint over the full-frame crop draft.


## Color grade (biltoo-1049 / 1050)

Tiles stay raw in the shared RAM cache. At paint time, `applyColorAdjustments`
runs on resolve (ContentXform or item grade). Converted `QImage`s (identity or graded) are cached per
item (`resolveGradedTile`) keyed by cell + grade signature so pan/repaint does
not re-copy or re-grade every frame. Soft underlay remains pre-graded from materialize.
