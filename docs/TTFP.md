# Time-to-first-pixel (TTFP)

## How to measure

```bash
BILTOO_TTFP=1 biltoo /path/to/album   # or BILTOO_PERF=1
```

Stderr reports stage deltas and a bar chart when the first
`installDisplayPixels` runs (or when the open path returns without pixels).

Example:

```
biltoo/ttfp: BEGIN finishApplyExpandedLoad
biltoo/ttfp: +  0.2 ms  Δ  0.2 ms  after_invalidateSessionLoads
…
biltoo/ttfp: + 12.0 ms  Δ  3.1 ms  installDisplayPixels
biltoo/ttfp: === report (first_pixels) total=12.0 ms ===
   0.2 ms |    after_invalidateSessionLoads (t+0.2)
   …
   3.1 ms |### installDisplayPixels (t+12.0)
```

## Critical path (Gallery, multi-image open)

```
expand (background)          ── TOC / member list
        │
applyExpandedPathsResult
        │
finishApplyExpandedLoad      ── GUI
  ├─ invalidateSessionLoads
  ├─ seedAppearances         (worker if n>24)
  ├─ sizesWarm?              N × Store get_size
  ├─ enterGalleryMode
  │    └─ setWorkspacePaths
  │         ├─ primeGeometry  N × size + LQIP blob
  │         ├─ size resolve?  (skip if warm)
  │         ├─ create items   O(n) ImageItem
  │         ├─ applyLayout    pack
  │         └─ decode window  install ImageCache → cells
  ├─ preparePaths            (no-op if warm plain files)
  └─ [async] ladderReady / TileSynth
        └─ installDisplayPixels  ← FIRST PIXEL
```

## Stages that used to dominate warm open

| Stage | Symptom | Status |
|-------|---------|--------|
| Forced RAR TOC refresh | Indexing archive every open | Fixed 1111 |
| `cachedSize` revalidate flood | Stats on every size hit | Fixed 1110 |
| `preparePaths` on GUI | Opening HUD + file stats | Fixed 1112 |
| Premature Opening message | HUD before warm check | Fixed 1114 |
| PDF layout on every `get_size` | N document opens | Fixed thumtoo 311 |
| Filmstrip before Gallery | O(n) list work first | Fixed 1114 (defer) |
| Size resolve + probes | Cold / process-memo cold | Store dims + light `get_size`; see **Baseline 2026-09-30** |
| Soft encode (SoftOnly) | Parallel encode | PreferCache / TileSynth |
| Tile pyramid cold | Zoom path | On-demand |

## Expected warm vs cold (order of magnitude)

Assumptions: local SSD, 94 prepared archive members, LQIP in Store.

| Stage | Warm | Cold |
|-------|------|------|
| Expand (Store TOC) | <5 ms | 50–500 ms+ (TOC walk) |
| sizesWarm ×94 get_size | 5–30 ms | same if sizes present |
| primeGeometry + LQIP decode | 10–80 ms | 0 if no LQIP |
| create 94 items + pack | 20–100 ms | same |
| first LQIP install | <5 ms after pack | — |
| first tile (overview) | cache hit <5 ms | encode 100 ms–seconds |
| first tile (zoom) | cache hit <5 ms | encode 100 ms–seconds |

**TTFP (warm, LQIP present)** should be dominated by **GUI item create + pack**, not I/O.

**TTFP (warm, no LQIP)** waits on first tile (or blank placeholder until tiles land).

**TTFP (cold)** waits on probe + extract + decode.

## Flamegraph interpretation

Bars are **Δ time of each stage**, scaled to total session time until first
pixels. Long bars early = GUI open path; long bar on `installDisplayPixels` =
waited on async decode (worker not GUI).

If `sizes_cold` appears on a prepared album, Store size rows are missing —
re-run `thumtoo-prepare --lqip --tiles …`.

## Real trace: 12 s between prime and finishSetWorkspacePaths

Cause: `pathContentId` hashed the outer archive **once per member** during
placeholder `installDisplayPixels` (appearance seed). Fixed in tip **1116**
(outer-file hash cache).

## Appearance (biltoo) vs locator (thumtoo)

Appearance rows live in biltoo state DB, keyed by **thumtoo locator.id**.
No content checksums. Open cost is URI → find_locator (Store), not file hashing.


## Layout-aware size gate (cold)

`layoutDefersPopulateUntilSizes`: only **MasonryFill / MasonryRowsFill / FlowFill**
wait for all sizes before populate. **Grid** and ordinary masonry pack with
provisional sizes immediately; probes still run; `sizeReady` debounces repack.


## Baseline 2026-09-30 — multipage PDF Gallery open

Recorded so a future regression has a concrete reference. Machine-dependent;
use **order of magnitude** and the **failure shapes**, not exact ms.

### Revisions

| Tree | Tip | Commit |
|------|-----|--------|
| **biltoo** | `2873.1-size-probe-cleanup` | `3907788a96313d2d4e3d357f0476873b073d9fba` (`3907788`) |
| **thumtoo** | `016.1-size-ensure-cleanup` | `d6a341f472879f55bd9c15176fc34f55bc3e3403` (`d6a341f`) |

Work-line bases: biltoo `ea477d6`, thumtoo `b825e8d` (full tip stacks in the
matching git bundles).

### Scenario

- Gallery open of a **session with a few PDFs**, about **2400 page refs** total.
- **Settled Store**: region width/height already written for (almost) all pages.
- **Process size memos cold** (new process or session replace) → TTFP path still
  reports `sizes_cold` even when SQLite is warm.
- Local disk (not NFS). Measure: `BILTOO_TTFP=1 biltoo …`.

### Measured TTFP (first `installDisplayPixels`)

| When | Total TTFP | Notes |
|------|------------|--------|
| **After this tip stack (good)** | **~0.45–0.55 s** | Example: **475 ms** total; ~43 ms through `finishApplyExpandedLoad_return`, ~430 ms until first pixels |
| Same stack, earlier good run | ~500 ms | Same shape |
| **Before region-dim + light size path** | **~10 s** | Size gate; low CPU; continuous climb with ~300-hit bursts and ~1 s gaps |
| **Broken serial hydrate (2870)** | **minutes** | Single-thread Store walk; aborted ~2 min at ~1000/2400 |
| **Region dims unread + tail ProbeSize** | **~90 s** | Fast to ~2300, then stall on last ~100; example **89059 ms** |

Representative **good** stderr shape:

```
biltoo/ttfp: sizes_cold
biltoo/ttfp: +  42.3 ms  …  finishApplyExpandedLoad_return
biltoo/ttfp: + 475.0 ms  …  installDisplayPixels
biltoo/ttfp: === report (first_pixels) total=475.0 ms ===
```

### What the good path does

1. `scheduleProbeBatch` → bounded parallel slots (`kMaxConcurrentSizeProbes`, 16).
2. Each slot: **`Client::get_size`** only (locator → media → **region width/height**).
   No `list_tile_scales`, no PDF open, no LQIP/EMB on this path.
3. **thumtoo must load region dims** in `Store::find_region` /
   `find_region_by_key`. If those columns are selected but not assigned, every
   page looks missing and the host re-probes (regression class: ~10 s or worse).
4. On true miss: `request_size` → **`ensure_pdf_page_sizes`** (one open, write all
   missing page dims for that file) → then `get_size` again; ProbeSize only if
   still empty.
5. Gallery **size gate** still waits until **all** pending sizes settle before
   full decode window; first pixels therefore track **gate completion**, not
   first size reply.

### Failure shapes to recognise

| Symptom | Likely cause |
|---------|----------------|
| TTFP ~10 s, little CPU, HUD climbs in ~300 steps with ~1 s pauses | Dual size walk (warm memos **and** probe FIFO) or heavy `request_size`+LQIP per page under low concurrency |
| TTFP minutes, steady but slow climb | Serial single-thread hydrate of all paths |
| Fast to ~N−100, then long stall to N | Missing region dims on a tail of pages; per-page ProbeSize / PDF open instead of `ensure_pdf_page_sizes` |
| `sizes_cold` every process start but Store is full | **Expected** — process memos empty; should still be **sub-second** if Store dims load |
| Warm open opens PDFs again for layout | `get_size` / `meta_from_store` falling back to live layout, or region dims not read from SQLite |

### Re-check

```bash
BILTOO_TTFP=1 biltoo /path/to/same-session
# Confirm tip SHAs:
git -C biltoo rev-parse HEAD
git -C thumtoo rev-parse HEAD
```

If total TTFP jumps from ~0.5 s to multi-second on the same machine and session
with a settled Store, treat as regression against this baseline before adding
new open-path work.
