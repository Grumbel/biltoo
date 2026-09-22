# Plan: hierarchical work status (CPU-visible)

**Status:** design only — no implementation required to read this doc.  
**Goal:** Whenever the machine is busy, the UI can answer *what* is working
(archive / image / operation), *how far*, and *why* CPU is high.

## 1. Problem today

Status is fragmented and lossy:

| Surface | What it knows | Gaps |
|---------|---------------|------|
| Centre "Resolving sizes…" | session-wide size gate counts | no URI, no archive, can freeze under load |
| Status bar (`updateStatus`) | session index, occasional errors | not a live work ledger |
| Filmstrip `loadsChanged` | thumb pending count | not merged with Gallery/Image |
| `ThumtooCache::sizeProbesBusy` | bool | no counts, no which paths |
| `Client::queue_stats()` | pending / inflight | no job *kind*, no URI, not surfaced in UI |
| `ContentStatus` in Store | per-content ready/failed | not a live activity stream |

Filmstrip can show names without sizes; Gallery size-gate still feels like a
black box. Archive extract is invisible. Tile pyramid work is invisible.

## 2. Design principles

1. **Single ledger, many views** — one in-process activity model; status bar,
   centre panel, and optional debug HUD are projections.
2. **Hierarchy** — Session → Archive (optional) → Image (URI) → Operation.
3. **Push from workers, pull for paint** — workers emit cheap events; UI
   samples at ~4–10 Hz (never per-tile full status rebuild).
4. **Identity** — always carry canonical URI (and archive root + member when
   applicable). No path-only guessing.
5. **Phase, not just counts** — each op has a phase enum (Queued / Running /
   Succeeded / Failed / Cancelled) plus optional `units_done` / `units_total`.
6. **thumtoo owns durable/cache work truth; biltoo owns session UX** — thumtoo
   reports *what the library is doing*; biltoo maps that onto session rows and
   mode chrome.

## 3. Activity model (shared vocabulary)

### 3.1 Operation kinds

```
SizeProbe          — get_size / request_size
SoftLadder         — soft / ladder pixels
TileCell           — single grid tile encode/fetch
TilePyramid        — host-side plan (many TileCell)
LqipEncode         — free-data LQIP
ArchiveOpen        — open container / TOC
ArchiveMemberRead  — extract or windowed read of one member
ArchiveScan        — sequential warm of many members
StoreIO            — SQLite blob read/write (optional, aggregated)
HostDecode         — biltoo-only: QImage path, gallery install, etc.
```

### 3.2 Records

- `ActivityId` — monotonic u64
- `parent_id` — optional (TileCell → TilePyramid, MemberRead → ArchiveScan)
- `kind`, `uri`, `archive_root`, `member_key`
- `phase` — Queued | Running | Succeeded | Failed | Cancelled
- `units_done` / `units_total` — optional
- `detail` — short machine string (error, scale, x,y)
- `started_ms` / `updated_ms`

### 3.3 Aggregates (computed)

- Per **archive_root**: members done/total, active kinds
- Per **uri**: dominant Running op
- **Global**: top-N running ops, queue depth by kind, primary status-bar story

## 4. thumtoo changes

### 4.1 API sketch

`include/thumtoo/activity.hpp`:

- `ActivitySnapshot snapshot_activity() const` — short critical section; copy
  of active + recent completed (ring buffer)
- Optional `set_activity_sink(callback)` for push (Qt bridge in biltoo)

Instrument job boundaries only (not per-pixel):

| Site | Kind |
|------|------|
| size probe worker | SizeProbe |
| soft / ladder jobs | SoftLadder |
| tile encode/fetch | TileCell |
| archive TOC open | ArchiveOpen |
| member extract | ArchiveMemberRead (+ parent ArchiveScan) |

### 4.2 Archive progress

- **Sequential:** parent `ArchiveScan` with `units_total = member_count`,
  children `ArchiveMemberRead` for the current window
- **Random:** show active member URIs + queue depth; no fake linear % without TOC

### 4.3 QueueStats

Extend or supersede with per-kind pending/inflight and `top_running[]` (~8).

## 5. biltoo changes

### 5.1 `WorkLedger` (host)

- Poll thumtoo snapshot at 5–10 Hz **or** Qt signals from `ThumtooCache::Bridge`
- Merge host-only work (size gate, filmstrip loads, tilelod, slideshow)
- Map URI → session index / `SessionImageId` when possible

### 5.2 UI projections

**A. Status bar (always on, compact)** — one line, e.g.

`Resolving sizes 128/1024 · zip: photos.zip 40/200 · tiles s=2 (12 inflight)`

Prefer heaviest Running kind; throttle text to ~4 Hz.

**B. Centre panel** — only for blocking gates; multi-line detail from ledger.

**C. Optional Activity debug** — `BILTOO_ACTIVITY=1` or View menu: top ops,
per-archive rollup.

**D. Later:** filmstrip/gallery cell badges (probe/tile/fail) if ledger lookup
stays O(1) by URI.

### 5.3 Fold existing HUDs into the ledger

Size-resolve centre detail, "Loading tiles…", filmstrip pending, and
`sizeProbesBusy()` gates should all read the same aggregates.

## 6. Phases

| Phase | Deliverable |
|-------|-------------|
| **0** | This doc + status-bar grammar agreement |
| **1** | thumtoo ActivityRegistry; instrument size probe; tests |
| **2** | biltoo ledger + status bar primary story |
| **3** | Archive-centric strings (root + member i/n) |
| **4** | Debug activity view / optional cell badges |
| **5** | Cap records, benchmark cold open 1k images |

**First PR slice:** size-probe only end-to-end (thumtoo snapshot → status bar).
Then tiles, then archives.

## 7. Non-goals (for now)

- Remote/multi-process activity
- Persisting activity to disk
- Per-tile on-canvas labels in production
- Replacing Store `ContentStatus` (lifecycle ≠ live work)

## 8. Success criteria

1. During 100% CPU open of a large folder **and** a large zip, the status line
   names a concrete archive or file and an operation kind within ~250 ms.
2. Size-resolve never shows stuck 0/N while probes complete.
3. Grid open shows tiles without blocking; status still reflects background work.
4. Debug UI off ⇒ only the compact status line.
