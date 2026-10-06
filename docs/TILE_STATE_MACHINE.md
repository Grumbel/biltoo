<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tile loading state machine (normative)

Supersedes the runtime/scheduling parts of [TILE_LOD.md](TILE_LOD.md)
(TileSession progressive climb, scale hold, generations, coverage heartbeat,
coordinator budgets). Geometry, planner, draw plan and paint rules in
TILE_LOD.md still apply.

Requires thumtoo with `Client::request_tile_cells` (thumtoo TILES.md,
"Interactive cell contract").

## Why the rewrite

Tile loading was one of the most patched areas (hundreds of "Fix: tile …"
commits: stuck InFlight, low-res forever, denser deadlocks, re-issue storms,
missing repaints). The audit below shows that most of them came from the same
structural problems: request state was shared between views but owned by none
of them, thumtoo could drop work silently, and failures had no reason or
retry rule. Each fix added a local gate or timer, which in turn caused new
corner cases.

### Audit — biltoo (pre-rewrite)

| # | Problem | Symptom |
|---|---------|---------|
| B1 | InFlight lived in the **shared** path cache, but a completion only reached the inbox of the session that issued it and was applied only by that session's `pump()`. Peers skipped InFlight keys. Filmstrip wake only repainted (no pump); prefetch slots died after ~1 s. | Cells stuck InFlight in other views ("stuck low res") |
| B2 | `cancel_obsolete()` and `~TileSession` cancelled/erased **cache-wide** `in_flight_keys()`, including peers' requests. Gallery virtual paint built a throwaway `TileLodController` per slot per frame, whose destructor cancelled the live cell's requests. | Requests cancelled and re-issued in a loop ("repeating") |
| B3 | Generation counters were per session but stored in shared entries and compared across sessions (Failed no-spam, stale-completion drop). LRU `last_used` mixed counters too. | Valid tiles dropped; failed cells never/always retried; arbitrary eviction |
| B4 | The draw plan cache was dirtied only by the session's own pump/viewport. Tiles landed by a peer, or evicted by a peer's trim, did not repaint. | "Not refreshing" |
| B5 | Tile grid size from soft samples (`tileNativeSize()` fell back to `imageSize()`), `logicalSizeForPath` (prefetch) or the size book (Gallery virtual). A different size cleared the **shared** cache for every view. | Tiles wiped / flapping, wrong cells requested |
| B6 | Gates that could never open: scale-0 needed a Ready coarser tile anywhere in the cache (a warm cache with only s=0 blocked s=0 forever); progressive climb needed ≥1 success at the held level (all-Failed coarse level stalled forever); denser gates similar. | Stuck at coarse / LQIP, no error |
| B7 | Failed was terminal for the "generation" with no retry timing, and thumtoo's `nullopt` carried no reason. | Permanent holes with no explanation |
| B8 | Polling: the 16 ms (Gallery 4 ms) tile timer re-armed while any visible item was not *fully covered*, so one failed cell spun it forever. Slideshow paint called `viewport()->update()` while not fully covered: a 60 fps repaint loop. | CPU burn on any failure |
| B9 | One `singleShot(0)` per completion, plus a nested 16 ms re-tick. The wake path called `tick(12)`, which issued requests outside the "sole issuer" coordinator. | Event storms, two issuers |
| B10 | Coordinator ticked only **one** target outside Gallery (Workspace with N items: N−1 never pumped except by wakes). | Workspace items stall |
| B11 | `pump()` capped at 16 completions per tick; the 8 ms coordinator throttle returned without re-arming. | Stalls under bursts |
| B12 | `invalidateAll()` cleared the registry map while controllers still held entries, so a later `release()` decremented a *new* entry's refcount. | Refcount drift, two caches per path |
| B13 | `has_succeeded_scale_ge()` scanned the whole cache once per visible key on every issue. | O(visible × cache) per tick |
| B14 | `set_viewport` rounded the viewport into a member but planned with the unrounded value (the comment claimed the opposite). Budget comment said 128 MiB, the value was 512 MiB. | Doc/code drift |
| B15 | `prepare_paths` emitted `sizeReady` without memoizing the size, so the GUI `cachedSize()` stayed invalid. | Tiles waited on a second probe |

### Audit — thumtoo (pre-rewrite)

| # | Problem |
|---|---------|
| T1 | `reply_cancelled_job` dropped tile callbacks **on purpose**: `cancel_uri`, `cancel_pending`, same-cell supersede, shutdown → silence. |
| T2 | `bump_interest_epoch` (every `set_interest`: Gallery scroll, filmstrip visible loads, Workspace selection) purged queued tile jobs silently. **Root cause of the classic "stuck InFlight forever".** |
| T3 | A worker exception skipped the callback. |
| T4 | Batches replied after **all** cells: first pixels waited for the slowest cell. The whole viewport ran as one job on one worker. |
| T5 | Every miss was `nullopt`: no difference between missing source, decode error, out-of-grid cell and refused denser scale. |
| T6 | Same-cell supersede across requesters dropped the earlier requester's callback. |
| T7 | `cancel_tile_cells` could not cancel cells inside batch jobs. |

All of T1–T7 are fixed by `request_tile_cells` (thumtoo `183d053`); B1–B15
are fixed by the design below.

## Design

```text
 view surfaces                    one per path                  global
 ─────────────                    ────────────                  ──────
 ImageItem ─┐                 ┌─────────────────┐
 filmstrip ─┤ TileSession     │   TileLoader    │      ┌───────────────┐
 slideshow ─┼─ plan + demand ─▶  demand union   │◀────▶│ TileScheduler │──▶ thumtoo
 prefetch  ─┤  draw + status  │  CellState map  │ pump │ (sole issuer) │   request_tile_cells
 Gallery   ─┘ (never issues)  │  inbox, epoch   │      └───────────────┘
   peek (passive)             └─────────────────┘
```

| Component | Owns | Never does |
|-----------|------|------------|
| `TileSession` (per view) | plan (target scale, visible keys), demand list, draw plan, `Phase` | issue, cancel, pump |
| `TileLoader` (per path, `TileLodRegistry`) | per-cell state + ticket, union of demand, results inbox, epoch, byte budget | decide global order |
| `TileScheduler` (process) | issue order across paths, global and per-path caps, wake timing | per-cell state |
| `ThumtooTileBackend` | `request_tile_cells` / `cancel_tile_cells` | state |

### Cell states (`CellState`, owned by `TileLoader`)

```text
  Missing ──issue──▶ Queued ──Ok──────────▶ Ready
  (no entry)          │  ├──Cancelled──────▶ Missing   (re-issued if demanded)
                      │  ├──Failed─────────▶ Failed    (attempts++, retry_at)
                      │  ├──Unavailable────▶ Unavailable (permanent)
                      │  └──stall timeout──▶ Failed    ("no reply after N ms")
  Failed ──retry_at reached & demanded──▶ Queued
```

Rules:

1. **One outstanding request per key.** `Queued` carries a ticket. Non-Ok
   results apply only to the matching ticket. `Ok` is always accepted for the
   current epoch (the pixels are valid even after a stall re-request).
2. **Only the backend or the watchdog leaves `Queued`.** Views never touch
   cells.
3. **Cancel = undemanded.** After demand changes, `maintain()` cancels
   `Queued` keys no view demands. The cell stays `Queued` until thumtoo
   answers `Cancelled` (→ Missing).
4. **Retry:** backoff 500 ms, 1 s; after `max_attempts` (3), a 30 s cooldown
   (transient NFS/IO recovers). `Unavailable` is never retried.
5. **Stall watchdog:** `Queued` for 45 s without any reply → `Failed` with
   "no reply from tile backend … (backend contract violation)", logged as
   `biltoo/tile: STALL …`. With the thumtoo contract this should never fire;
   if it does, the log says so.
6. **Epoch:** `invalidate()` (reload, session replace) and a content-size
   change cancel everything, drop all cells and bump the epoch, so late
   results are dropped.
7. **Content size = authoritative native only** (`ThumtooCache::cachedSize`).
   A change is recorded (`content_changes`, `content_change_note`). Repeated
   changes mean a host bug and show up in `BILTOO_TILE_DEBUG`.

### Demand (per view, `TileSession`)

- Target scale from density (`target_scale_for_density`, clamped
  `[min_scale, max_scale]`), with no hold or progressive gating.
- Demanded levels: target `T` plus overview `T+1`, `T+2` (capped at
  `max_scale`) for rasters. For live denser `T < 0`: `{0, T}` only, because
  intermediate denser levels each re-raster the page in thumtoo.
- Priority = `class·10¹² + coarseness·10⁹ − distance-to-centre`. Classes:
  prefetch 0, filmstrip 1, Gallery/Workspace 2, Image/slideshow 3. Coarse
  cells come first, so the first pixels are an overview and arrive fast. No
  gate can deadlock because nothing waits on anything.
- **Demand is a lease** (4 s). Visible views renew it (paint, coordinator
  tick, filmstrip renew timer, slideshow tick, prefetch tick). A view that
  disappears without saying so stops renewing and its cells get cancelled.
  A lapsed view re-publishes on its next renew.
- Refused denser scales (image-heavy PDF page whose full-page raster exceeds
  thumtoo's budget) come back **Unavailable** before any render. When the
  whole visible target is Unavailable, the controller raises the page's
  denser floor one step and re-plans (`denserScaleFloor`).
- **Passive** sessions (Gallery virtual-slot peek) plan and draw but never
  publish demand, so creating or destroying them cannot affect loading.

### Scheduler

`TileScheduler::pump()` runs on the GUI thread: for each loader it applies
results and runs `maintain()`, then collects issuable cells from all loaders,
sorts them by priority, and issues within `max_queued_global` (48) and
`max_queued_per_loader` (32), in chunks of 4 cells per thumtoo job (parallel
workers, coarse chunks first). Then it arms the next wake at the earliest
loader deadline (retry, stall, lease) that lies in the future.

It is event-driven, with no polling. A result asks for a wake at 0 ms; a
demand change asks for one after a 24 ms settle (coalesces wheel zoom). The Qt
driver (`installTileSchedulerQtDriver`, installed lazily by the registry) is a
single-shot timer. A deadline that is already due but blocked by the cap never
causes a zero-delay wake, because the result that frees capacity wakes the
scheduler anyway.

### Status / feedback

`TileSession::Phase`:

| Phase | Meaning |
|-------|---------|
| Idle | no content size / nothing visible |
| Loading | a visible cell is Missing, Queued or Failed with retries left |
| Complete | every visible cell Ready at the target scale |
| Degraded | done; some cells Failed (exhausted) / Unavailable; parents cover them |
| Error | done; some visible area has no pixels at all |

`first_error()` / `status_line()` return the backend's reason. ImageItem draws
a banner on the visible part of the image for Degraded ("Showing lower
resolution: …") and Error ("Tiles failed: …"). The debug overlay
(`BILTOO_DEBUG_OVERLAY` / `BILTOO_TILE_DEBUG`) shows phase, counts and reason.
`BILTOO_TILE_DEBUG=1` prints `biltoo/tile-sched:` per pump with work, and
`biltoo/tile-coord:` item lines with phase, counts, loader issued / stale /
stalls / cancels / sizeChanges and the error.

### Host wiring

- `ImageItem::prepareTileLodPlan` → plan + demand (Image mode = focus
  class). `installTileChangeHook` repaints, coalesced, on loader changes.
- `ImageItem::tickTileLod` → plan + lease renew. When the item leaves the
  tile band it calls `clear_demand()`.
- `DisplayPipelineController::tickPrimaryTileLod` re-arms at 250 ms only
  while a visible item is Loading or has no controller yet.
- `TileLoadCoordinator` refreshes every visible candidate (no budgets, no
  single-target limit) and cancels PreferCache once per path.
- Slideshow / filmstrip / prefetch: `setOnChange` repaint, `refresh()` to
  renew. Prefetch lives ≤ 15 s at speculative priority.
- `sizeReady` memoizes the size and ticks the tile band. Tiles wait for the
  authoritative native size.

### Tests

`tests/tilelod_test.cpp` (Qt-free, deterministic clock) pins each bug:
two views share one request (B1), destroying a peer/passive view never
cancels (B2), draw plan follows loader changes (B4), size change drops cells
and is recorded (B5), coarse-first order with no gates (B6), backoff → Error
with the reason → cooldown retry (B7), no busy wake at the cap (B8),
Cancelled-while-demanded is re-issued (T2), stall watchdog with a late Ok,
invalidate drops late results, lease expiry, priority across paths, Ok
without pixels → Failed, denser demand levels.
thumtoo `tests/test_tile_cells_contract.cpp` pins exactly-once delivery
under cancel, epoch bump and shutdown.
