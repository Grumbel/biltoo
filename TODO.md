# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2382-cancel-size-probes-sizes-first** (on top of `660c49c` stack).

Includes **2381** (unused provCell).

### Fixes
1. **Session replace:** `ThumtooCache::cancelSizeProbes()` clears the host size
   probe FIFO and bumps a generation so in-flight Store size callbacks cannot
   emit `sizeReady` or refill `ImageCache` for the previous path set (old thumbs
   in a new session). Wired from `ImageView::invalidateSessionLoads`.
2. **Sizes before layout:** removed the open probe-prefix (128) truncation of
   the Gallery size-resolve gate. Packaged layouts need real sizes for the full
   session; concurrency stays bounded (`kMaxConcurrentSizeProbes`) and
   `sizeReady` stays chunked.

**Thumtoo pinned** in `flake.lock` → `f71d183` (thumtoo-323).
Source layout 0.2.0 complete (phases 1–31).

**Next (release path):**
1. RC smoke (open, Gallery, crop, export, shell icons after `.qrc` move;
   verify session switch drops old filmstrip/gallery pixels; cold open packs
   after sizes)
2. VERSION 0.2.0 + tag

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2382.1-cancel-size-probes-sizes-first-660c49c.bundle HEAD
```

## Backlog (0.2.0)
- [x] src subdirs phases 1–31 (layout complete)
- [x] pin thumtoo ≥ 323 (`flake.lock` → f71d183)
- [x] Resolving sizes… HUD top-left (non-blocking)
- [x] drop unused provCell in ThumbnailBar::setFiles (2381)
- [x] cancel size probes on session replace; sizes before layout (2382)
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag

## Notes (0.2 interaction)

### Cold-cache scroll / request flood

Gallery decode-window already debounces scrollbar (`kDecodeWindowSettleMs`).
Image/Workspace used to call `tickPrimaryTileLod` on **every** `valueChanged` /
pan move — that flooded EnsureTiles on cold cache. Now coalesced via
`scheduleTileLodAfterInteraction(32)` (tile tick + climb on settle).

Remaining cold-cache jank is often **worker/Store pressure** after the settle
fires (legitimate visible cells), not missing debounce — profile under
`THUMTOO_DEBUG` if UI still stalls once scrolling stops.

## Backlog (0.3.0)

### Gallery size-resolve throughput (plain files / SSD)

**Observation:** During “Resolving sizes…” CPU is often mostly idle even for
plain images on local SSD. Archives may differ (TOC / extract).

**Current design (already concurrent, still feels serial):**
- Host: `ThumtooCache::scheduleProbeBatch` → FIFO with
  `kMaxConcurrentSizeProbes = 8` (`src/host/thumtoo_size_probe.cpp`).
- Thumtoo: `Client::open(..., worker_threads=0)` → `hardware_concurrency()`
  workers; `request_size` is queue + Store.
- Warm memos emit `sizeReady` in chunks of 16 to keep the GUI responsive.

**Likely bottlenecks to profile (not fixed in 0.2):**
- SQLite / Store lock serialization on cold `request_size` (workers wait).
- Host probe concurrency vs Store contention — raising 8 alone may not help.
- Fast path: header/EXIF/libvips size without full Store register for plain
  files (thumtoo already has EXIF embedded preview on size probe — extend?).
- Archives: member extract coalesce vs per-member size cost (document breakdown).

**0.3 approach:** measure (probe wall vs Store vs decode), then either raise
bounded concurrency with evidence, or a dedicated plain-file size fast path
that does not serialize on the Store write lock.

### Size-resolve failure diagnostics

**Observation:** HUD shows “N failed” with no path list or reason.

**Current path:** failed probe → `sizeReady(path, QSize())` →
`GallerySizeResolve` increments `m_failed` only. No error string on the
signal; tooltip on items is generic (“Failed to read image size”).

**0.3 approach:**
- Thread a short reason through thumtoo `SizeReply` / host `sizeReady` (or a
  parallel `sizeProbeFailed(path, reason)` signal).
- Keep corner HUD compact: “N failed” + optional first path; full list in
  status bar / log / debug overlay (`BILTOO_*` / `THUMTOO_DEBUG`).
- Do not spam a modal per failure on large sessions.

### ImageView ownership extraction (planned)

Do **not** dump `imageview_*.cpp` into a folder without transferring ownership.
Continue the Phase 5 controller pattern: narrow Host API + collaborator owns
state; `ImageView` remains the public QGraphicsView surface.

| Collaborator (working name) | Pull from | Notes |
|-----------------------------|-----------|--------|
| Paint / overlay host | `imageview_paint*`, background overlays | Orchestration only; thumtoo keeps pixel stamps |
| Input router | `imageview_input*` | Dispatch + mode gate; policies already elsewhere |
| Session bind/remove | `imageview_session_*`, size book | Identity rules stay in IDENTITY.md |
| Rematerialize / bake host | `imageview_rematerialize`, `imageview_bake` | Display pipeline remains display/ |

**Optional small win:** split `imageitem_interaction.cpp` (~1.8k lines) if
interaction work resumes.

**Also 0.3 product:** dual ImageView / two-up compare — [RELEASE_0.2.0.md](docs/RELEASE_0.2.0.md) §4.8.

See [REFACTOR.md](REFACTOR.md) (post–Phase 5) and [docs/SRC_LAYOUT.md](docs/SRC_LAYOUT.md).
