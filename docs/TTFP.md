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
| Size resolve + probes | Cold only | Expected when Store empty |
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
| first soft PreferCache | — | 50–300 ms / image |
| first tile (zoom) | cache hit <5 ms | encode 100 ms–seconds |

**TTFP (warm, LQIP present)** should be dominated by **GUI item create + pack**, not I/O.

**TTFP (warm, no LQIP)** waits on PreferCache / TileSynth worker after schedule.

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

## Appearance keys (path identity)

Default `pathContentId` never reads file bytes — key is path+size+mtime
(+ page/member). Optional `BILTOO_CONTENT_HASH=1` enables full-file SHA-256,
cached until size/mtime change.
