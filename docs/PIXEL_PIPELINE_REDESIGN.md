<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Pixel pipeline redesign (thumtoo first)

**Goal:** minimise time-to-pixel-on-screen for both low-resolution placeholders
and high-resolution display, with a clean quality model, tile-backed arbitrary
ladder levels, and archive-aware parallel work lanes.

**Scope of this document:** lower layers (thumtoo cache, decode, tiles, quality
provenance, scheduling contracts). GUI (Gallery / filmstrip / slideshow) is
only described as a *consumer* of the API. Do not start with more GUI
debounce; fix the pipeline under it.

**Non-goals (for this design):**

- Replacing biltoo’s view modes or layout algorithms.
- Network / remote image sources.
- Lossless archival of every intermediate raster.

---

## 1. Problem statement

### 1.1 Current shape

Today the host effectively has three blunt instruments:

| Instrument | Typical edge | Cost | Quality |
|------------|--------------|------|---------|
| Embedded / EXIF thumb | ≤160–320 | Cheapest | Unreliable (crop, colour, aspect) |
| Soft durable ladder | ≤512 | Medium (encode + DB) | Good overview |
| Full native decode | native | Expensive | Best |

Anything between 512 and native is ad hoc: shrink-on-decode in biltoo,
one-off archive extracts, or accidental full decode. That causes:

- **Slow time-to-soft** when soft levels are missing and the only fallback is
  full extract of an archive member.
- **Slow time-to-sharp** when the UI asks for ~1024–2048 and either waits on
  soft max or jumps to full decode.
- **Queue collapse** under scroll: many independent full/soft requests,
  FIFO worker queues, no quality-aware replacement, no archive batching.
- **Quality lies**: EXIF stand-ins treated like ladder pixels; soft promoted
  to “decoded”; full never cleared.

### 1.2 Archive constraint (hard)

libarchive **streams**. Random member access means re-reading from the start
(or maintaining expensive multi-handle state). Consequences:

- N independent “decode member i” jobs on the same `.cbz` ≈ N sequential
  scans → **super-linear wall time** under scroll thrash.
- The only efficient cold path for many members is **sequential batch
  extraction** of a window of interest, not N parallel seeks.
- Once bytes (or tiles) for a member exist in process or durable cache,
  subsequent ladder/tile work must **not** re-stream the archive.

### 1.3 Design principle

> **Pixels are produced by lanes with explicit quality and cost. Higher quality
> replaces lower quality; lower quality never blocks higher quality from
> starting. Interest (what the user needs *now*) owns the workers.**

---

## 2. Quality model

Every pixel buffer that can appear on screen carries a **quality tier** and a
**provenance**. Tiers are totally ordered for replacement decisions.

### 2.1 Tiers (low → high)

| Tier | Name | Typical source | Trust | Use |
|------|------|----------------|------|-----|
| **Q0** | `EmbeddedThumb` | EXIF / APP1 / container preview | Low | Optional first paint only; never layout truth alone if size known |
| **Q1** | `FastScale` | Shrink-on-decode (JPEG `scale_denom`, `vips_thumbnail`, PDF low-DPI raster) | Medium | Batch overview, cold gallery, filmstrip soft |
| **Q2** | `TileReconstruct` | Assembled from **high-quality** tile pyramid | High | Arbitrary ladder edge without full framebuffer |
| **Q3** | `FullRaster` | Native decode (or lossless full tile set equivalent) | Highest | Image mode, export, accurate colour ops |

Rules:

1. **Replace upward only** for the same locator and target edge band:
   Q0→Q1→Q2→Q3. Never replace a Q2 buffer with Q1 for the same edge.
2. **A higher tier at a smaller edge can still be shown** while a lower tier
   at a larger edge is loading (e.g. Q2@512 on screen while Q2@2048 builds).
3. **Embedded thumbs (Q0)** must be tagged and never written into the durable
   soft ladder as if they were Q1/Q2.
4. **FastScale (Q1)** tiles and rasters are marked `quality=fast` so a later
   full-res pass can **invalidate or supersede** them.

### 2.2 Provenance record (required metadata)

```text
PixelProvenance {
  locator:        Uri              // file / archive member / page
  long_edge:      int              // actual pixels long edge
  tier:           Q0|Q1|Q2|Q3
  source_kind:    Embedded | JpegScale | PdfLowDpi | TileSynth |
                  FullDecode | SoftDurable
  tile_gen:       uint64           // pyramid generation (0 if not from tiles)
  source_mtime:   optional stamp
  incomplete:     bool             // true if assembled with missing tiles
}
```

Hosts must store provenance with the pixmap (or a side map keyed by path).
UI decisions (upgrade, skip, trust for crop) use provenance, not heuristics
on pixel dimensions alone.

---

## 3. Tile pyramid (thumtoo core)

### 3.1 Why tiles

A durable **tile pyramid** lets thumtoo answer:

> “Give me a raster whose long edge is ≈ E”

without:

- holding a full native framebuffer for every page, or
- being limited to fixed soft steps {128,256,512}, or
- re-decoding the source for every edge.

Fixed soft JXL levels remain useful as **optional durable snapshots** of
common edges; they are not the only way to serve pixels.

### 3.2 Pyramid structure

For each locator that has been through a **full-res tile build**:

```text
Level L=0: full-resolution tiles (tile size T×T, e.g. 256)
Level L=1: ½ resolution tiles (from L=0 or from decode mip)
…
Level L=n: until max(w,h) ≤ T
```

Optional: store only L=0 and synthesise upper levels on demand (CPU vs disk
trade-off). Recommended default: store L=0 heavily compressed + a few mid
levels that match common UI edges (256, 512, 1024).

### 3.3 Constructing an arbitrary ladder image

```text
request_raster(locator, target_long_edge, min_tier) ->
  if durable soft/JXL exists at edge ≥ target and tier ≥ min_tier:
      return decode(durable)
  if tile pyramid exists:
      return blit_cover_from_tiles(pyramid, target_long_edge)
  return not_ready
```

`blit_cover_from_tiles`:

1. Choose pyramid level L whose native long edge is ≥ target (or best available).
2. Map the full image rectangle to tiles at L.
3. Decode only needed tiles (usually all for a downscaled full-frame preview).
4. Scale/composite to exactly the requested long edge (or power-of-two step).
5. Attach provenance `tier=Q2`, `source_kind=TileSynth`, `tile_gen=…`.

This is the **primary** path for Gallery/filmstrip edges 128–2048 once tiles
exist. It is much cheaper than full decode and sharper than Q0/Q1 stand-ins.

### 3.4 Quality of tiles

| Tile source | Tier of resulting raster | Notes |
|-------------|--------------------------|-------|
| Full-res decode → tile | Q2 (and base for Q3) | Canonical pyramid |
| Fast JPEG scale → tile | Q1 | **Marked low quality**; must not block Q2 rebuild |
| Embedded thumb only | Q0 | Prefer **not** to tile; one-shot pixmap |

**Invalidation:** when a Q2/Q3 pyramid appears for a locator, all Q1 tiles and
rasters for that locator are obsolete for edges ≤ pyramid coverage.

---

## 4. Work lanes (parallel generation)

Two complementary lanes share the same locator space but different goals.

### 4.1 Lane A — Batch fast overview (`FastBatch`)

**Purpose:** minimise time-to-first-pixels for **many** locators (gallery open,
archive browse, filmstrip fill).

**Characteristics:**

- Windowed interest: visible ∪ overscan ∪ explicit prepare set.
- **Sequential archive pass** when locators share one container (see §5).
- Decode shortcuts allowed:
  - JPEG scaled decode (libjpeg scale_num/scale_denom, or vips thumbnail).
  - PDF raster at low DPI.
  - Skip colour-managed luxury paths.
- Output long edge capped at **`batch_max_edge`** (default **1024**,
  configurable; never above this in Lane A).
- Quality: **Q1** (`FastScale`), provenance mandatory.
- May write **optional** durable soft snapshots at 256/512 for reuse.
- Must **not** claim Q2 tile pyramid unless pixels truly come from full-res
  tiles (they do not).

**Concurrency:** one batch worker **per archive** (or global single archive
cursor); multiple archives may batch in parallel with a small cap.

### 4.2 Lane B — Focus high quality (`FocusFull`)

**Purpose:** minimise time-to-sharp for the **few** locators the user is
actually looking at (current image, focused gallery tile, slideshow from/to).

**Characteristics:**

- Interest size tiny: typically 1–3 locators.
- Full decode (or high-DPI page raster).
- Build / upgrade **Q2 tile pyramid** from full pixels.
- Then `request_raster` at display edge is Q2 tile reconstruct (fast).
- Quality: **Q3** framebuffer optional to keep in memory; durable value is
  the pyramid.
- Preempts Lane A for the same locator: cancel or demote fast work; do not
  wait for batch to finish before starting focus.

**Concurrency:** 1–2 focus decodes process-wide so memory stays bounded.

### 4.3 Lane C — Interactive construct (no new decode)

**Purpose:** serve `request_raster` when tiles or durable soft already exist.

- CPU-only tile blit / JXL decode.
- High concurrency OK (bounded by thread pool).
- Never streams archives.

### 4.4 Priority between lanes

```text
FocusFull (B)  >  Interactive construct (C)  >  FastBatch (A)
```

Within a lane, **current interest epoch** wins; stale jobs discard results.

---

## 5. Archive streaming model

### 5.1 Single cursor per archive

For each open archive path:

```text
ArchiveCursor {
  path
  ordered_members[]          // TOC order (libarchive sequential order)
  next_index                 // sequential read position
  inflight_batch             // optional window being extracted
}
```

**Rule:** only one sequential extract stream per archive at a time in Lane A.
Lane B focus extract may open a **second** pass only when necessary; prefer
hitting process LRU of already extracted member bytes first.

### 5.2 Batch window algorithm

Given interest set I of member indices (sorted):

1. Clamp I to a maximum window size W (e.g. 32 members) around the viewport.
2. If cursor is far from min(I), either:
   - **Skip-forward** by reading headers only until min(I) (cheap), or
   - Restart from beginning if skip is impossible/expensive (measure; prefer
     skip-forward when libarchive allows).
3. Extract members in order until max(I) or budget exhausted.
4. For each extracted member: run Lane A fast decode → publish Q1 raster ≤
   `batch_max_edge`; optionally enqueue soft durable encode **idle-only**.
5. Member compressed bytes go to **process LRU** (existing biltoo member LRU
   idea) so FocusFull does not re-stream if batch just touched the member.

### 5.3 Focus member in a large archive

1. Check process LRU / durable pyramid / soft DB.
2. If miss: schedule FocusFull extract. If Lane A holds the archive cursor and
   is far away, **do not wait** — start a dedicated extract for that member
   (accept one extra scan) **or** request Lane A to jump (header skip) to that
   member with high priority, then resume batch.
3. After full decode: build Q2 tiles; publish; Lane C serves further edges.

Policy choice (implement explicitly, default recommended):

> **Priority jump of the single archive cursor toward the focus member**,
> then resume batch behind the viewport. Second concurrent archive handle only
> if jump latency exceeds a threshold.

### 5.4 Why this fixes “worse the longer I scroll”

Scroll currently enqueues many independent member decodes. Each restarts or
contends stream position. The cursor model turns scroll into **window
updates** against one sequential reader, and drops members that left the
window from the batch plan (not from durable cache once written).

---

## 6. Unified request API (thumtoo)

Hosts should stop calling a scatter of `get_pixels` / `request_pixels` /
ad hoc full loads without tier. Target client API:

### 6.1 Interest

```text
set_interest(InterestSnapshot {
  epoch: u64
  items: [{ locator, target_long_edge, role: Primary|Near|Speculative }]
})
```

- Replacing interest **cancels** work not needed for the new snapshot
  (generation/epoch).
- Primary drives FocusFull; Near drives FastBatch window; Speculative is
  idle-only.

### 6.2 Raster request

```text
request_raster(locator, target_long_edge, {
  min_tier: Q0|Q1|Q2,
  prefer_cached: bool,
  deadline_ms: optional
}) -> handle
```

Completion (async, executor):

```text
RasterReady {
  locator, long_edge, image_bytes or decoded buffer,
  provenance: PixelProvenance,
  epoch
}
```

Stale epoch → host discards.

### 6.3 Cache probes (sync, cheap)

```text
peek_size(locator) -> optional Size
peek_raster(locator, max_edge, min_tier) -> optional { bytes, provenance }
```

No scheduling. UI open path uses peek for LQIP/size/instant soft.

### 6.4 Explicit focus

```text
request_focus(locator, {
  build_tiles: true,
  keep_full_framebuffer: bool
})
```

Ensures Lane B runs even if interest edge is small (Image mode).

### 6.5 Cancellation

```text
cancel(handle | locator | epoch)
```

Cancel must free worker slots; discarded CPU results are OK, but **archive
cursor state** must remain consistent (do not leave a half-read stream without
a defined resume policy).

---

## 7. Durable store layout (conceptual)

```text
cache_root/
  locators.db            // size, mtime, flags, unsupported
  soft/                  // optional JXL levels ≤ soft_max (512 default)
  tiles/{locator_hash}/  // pyramid levels, quality flag per level
  lqip/                  // tiny previews
  toc/                   // archive TOC
```

**Tile level metadata:** `quality=full|fast`, `gen`, `tile_size`, `z`, `w`, `h`.

**Migration:** existing soft-only caches remain valid as Q1/Q2-equivalent
durable soft; tile dirs appear as FocusFull runs.

---

## 8. Configuration

| Knob | Default | Meaning |
|------|---------|---------|
| `batch_max_edge` | 1024 | Cap for Lane A FastScale output |
| `soft_durable_max_edge` | 512 | Max edge written as classic soft ladder |
| `tile_size` | 256 | Pyramid tile extent |
| `batch_window` | 32 | Max members per archive batch plan |
| `focus_concurrency` | 1 | Parallel FocusFull jobs |
| `batch_concurrency` | 1 per archive, 2 archives | Lane A caps |
| `construct_concurrency` | 4 | Tile blit / soft decode |

Environment mirrors (suggested): `THUMTOO_BATCH_MAX_EDGE`, etc.

---

## 9. Time-to-pixel paths (happy cases)

### 9.1 Cold gallery, loose JPEG files

1. `peek_size` / LQIP from DB or probe (Lane A probe).
2. FastScale decode ≤1024 (Lane A) → paint Q1.
3. Idle: soft durable 256/512.
4. Focus tile: FocusFull → tiles → Q2 reconstruct at display edge.

### 9.2 Cold gallery, large CBZ

1. TOC from durable or one archive scan.
2. Interest window → **one** sequential batch extract + FastScale ≤1024.
3. Visible members paint as each member completes (progressive).
4. Focus page: prioritise cursor / dedicated extract → full → tiles → Q2.

### 9.3 Warm cache, zoom gallery

1. `peek_raster` / tile construct at new edge (Lane C) — no archive I/O.
2. If tiles missing, FocusFull only for visible primaries.

### 9.4 Slideshow flip

1. Keep last frame (host).
2. Interest Primary=next; Lane C or Q1 immediate if cached.
3. FocusFull only if Q2/Q3 missing; ZoomBlur is host-side and must not
   compete with Lane B (separate tiny budget or derive from Q1).

---

## 10. Host (biltoo) integration contract

Until GUI is reworked, biltoo should treat thumtoo as:

1. **Single scheduler façade** — all soft/display requests go through
   `set_interest` + `request_raster` (or today’s API with the same semantics
   once implemented).
2. **Provenance-aware install** — `ImageItem` / filmstrip store tier; upgrades
   only.
3. **No parallel private QThreadPool pipelines** that decode the same archive
   members (slideshow preload, gallery full, filmstrip) without coordinating
   interest.
4. **Gallery placeholders** = Q0/Q1 only; sharp tiles = Q2 from construct.

Detailed GUI rewrite is a **later document**; it must not invent new decode
paths.

---

## 11. Implementation phases (lower levels first)

### Phase 0 — Spec lock

- [ ] Freeze quality enum + provenance struct in thumtoo public headers.
- [ ] Document archive cursor single-stream invariant.
- [ ] Agree `batch_max_edge` default 1024.

### Phase 1 — Provenance + peek

- [ ] Tag all existing soft / embedded / full outputs with tier.
- [ ] `peek_raster` returns provenance; hosts can refuse Q0 for layout.

### Phase 2 — FastBatch lane

- [ ] Archive cursor + windowed batch extract.
- [ ] FastScale decode ≤ `batch_max_edge`.
- [ ] Process member byte LRU shared with focus.
- [ ] Interest cancellation drops out-of-window members from the plan.

### Phase 3 — FocusFull + tile pyramid

- [ ] Full decode → tile writer (Q2).
- [ ] `construct_from_tiles(target_edge)`.
- [ ] Invalidate Q1 outputs when Q2 pyramid appears.

### Phase 4 — Unified request API

- [ ] `set_interest` / `request_raster` / cancel-by-epoch.
- [ ] Retire unconstrained FIFO `request_pixels` queue or make it a thin
      wrapper over interest.

### Phase 5 — Durable soft as optional cache of constructs

- [ ] Write 256/512 soft from Q1 or Q2 when idle.
- [ ] Prefer Q2 construct over re-decode when tiles exist.

### Phase 6 — Host adoption (biltoo)

- [ ] Gallery/filmstrip/slideshow speak interest + provenance only.
- [ ] Remove competing ad hoc full loads for overview.
- [ ] Update `docs/GALLERY_SOFT.md` to point here.

Each phase must keep **time-to-first-pixel** ≤ status quo for warm soft hits;
Phases 2–3 are what fix cold archives and mid-edge quality.

---

## 12. Testing plan

| Test | Expect |
|------|--------|
| Cold CBZ, 200 pages, open gallery | Visible window Q1 within sequential batch; no N full-file restarts |
| Scroll away and back | Warm construct or soft; no queue meltdown |
| Focus one page mid-archive | Q2/Q3 without waiting whole batch |
| `request_raster` 1536 with tiles | Q2 blit, no full framebuffer required |
| Q1 then Q2 same path | UI upgrades once; never Q2→Q1 regress |
| Embedded thumb only | Marked Q0; replaced when FastScale or tiles arrive |
| Cancel interest mid-batch | Workers free; cursor left in defined state |
| Two archives | Independent cursors; focus cap respected |

Stress: hold arrow key on gallery selection through a huge CBZ; queue depth
and extract count must stay O(window), not O(history).

---

## 13. Risks and decisions

| Risk | Mitigation |
|------|------------|
| Tile disk growth | Cap pyramid depth; LRU tile locators; compress tiles |
| Double scan on focus | Prefer cursor jump; measure before always opening 2nd handle |
| FastScale colour mismatch | Document; FocusFull is source of truth for colour-critical ops |
| Host ignores provenance | Make tier visible in debug overlay; assert in debug builds |
| Complex API | Ship Phase 2–3 behind existing `request_pixels` wrapper first |

**Open decision:** second concurrent archive handle for focus vs cursor jump
only — default **cursor jump**, fallback second handle after timeout.

---

## 14. Summary

```text
                    interest epoch
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
     FastBatch        FocusFull      Construct
     Q1 ≤1024         Q3 + tiles     Q2 from tiles
     archive cursor   preemptive     CPU only
          │               │               │
          └───────────────┴───────────────┘
                          │
                   RasterReady + provenance
                          │
                      biltoo UI
```

**Lower level first:** quality tiers, tile pyramid, batch vs focus lanes,
archive cursor. **Then** GUI. Time-to-pixel is a property of this pipeline,
not of more timers in Gallery paint.
