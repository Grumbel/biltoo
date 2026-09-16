<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Performance model (pixels, ladder, tiles, archives)

Order-of-magnitude guidance for biltoo + thumtoo. Not lab microbenchmarks;
use it to choose paths, not to quote absolute milliseconds.

Related: [**THUMTOO_HOST_CONTRACT.md**](THUMTOO_HOST_CONTRACT.md) (normative request/delivery),
[GALLERY_SOFT.md](GALLERY_SOFT.md), [PIXEL_PIPELINE_REDESIGN.md](PIXEL_PIPELINE_REDESIGN.md),
[PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md), thumtoo `TILES.md`.

---

## 1. Ladder bands (what a request actually hits)

| Long edge | What it is | Mechanism |
|-----------|------------|-----------|
| **≤512** | **Session soft** | `schedulePixels` / `get_pixels` / `cachedLadderBytes`. Cap = thumtoo `kMaxSoftLadderEdge` = biltoo `kGalleryLadderEdge`. Session encode; not schema-4 durable levels. |
| **~1024** | **FastBatch overview (Q1)** — *not* a soft level | `scheduleOverviewPixels` → `request_overview_pixels` / `RasterPolicy::Overview`. Often **jpeg_shrink** (DCT scale). `kBatchOverviewEdge`. |
| **≤2048** | **PreferCache display** | `scheduleDisplayPixels` / PreferCache. Soft or overview if present; else **tile reconstruct** when a pyramid exists. `kImageLadderEdge`. |
| **Near native / full** | **Full / FocusFull** | `scheduleFullPixels` / `request_full_pixels`, or full decode → tile pyramid. ImageCache host store still clamps ~2048. |

`kLadderEdges[] = {128, 256, 512, 1024, 2048}` is for **snapping on-screen need** (`ceilLadderEdge`), not “durable soft at every step.” Soft never stores 1024.

**Bug pattern to avoid:** full want edge ≈ native (e.g. 3056) while thumtoo only returns jpeg_shrink overview (≤1024). Delivery must **settle** that full request so biltoo does not loop `ladderReady → tryInstall REJECT → scheduleFull`.

---

## 2. Baseline: raw pixels vs codecs

Relative to **memcpy / QImage assign** of an already-decoded buffer (**1×**):

| Operation | Rough cost |
|-----------|------------|
| ImageCache hit → paint | ~1–2× |
| JPEG full decode (libjpeg-turbo) | ~5–20× same pixel count |
| JPEG XL lossy decode (multi-thread) | often similar to JPEG; single-thread can be slower |
| JPEG XL lossless decode | often slower than PNG single-thread; multi-thread helps |
| JPEG XL **encode** fast effort | background soft ladder OK |
| JPEG XL encode high effort | offline only — not UI path |

**Once pixels are in `ImageCache` / the item, reuse is essentially free next to any codec.**

---

## 3. Progressive load: pay 1+4, show earlier

| Strategy | Total work | First pixels |
|----------|------------|--------------|
| Full only | **4** | After full finishes |
| Overview **then** full | **1 + 4 = 5** | After **1**, upgrade after **4** |

About **+25%** total CPU for **~4× sooner** first paint (when overview is ~¼ the linear size → ~¼ the pixel work). Correct for a viewer if:

- the **1** is truly cheap (DCT shrink / soft), and
- shortfalls **settle** (no re-queue spin), and
- cache is upward-only.

If the user leaves before full arrives, only **1** (or partial **4**) was spent.

---

## 4. JPEG scale-from-large vs native small file

Same **output** long edge ≠ same cost.

Example: **4000×6000** JPEG → ~**1024** long edge vs a file that is already ~1024.

| Path | Dominant cost |
|------|----------------|
| Native ~1024 JPEG | Small bitstream + few MCU blocks |
| Big JPEG + `scale_denom` / jpeg_shrink → 1024 | **Still entropy-decode the large bitstream**; IDCT/output ≈ small |

Ballpark: scale-from-large often **~2–5×** slower than a true 1024 asset; still **much** better than full 24 MP decode + resize.

Cost model:

```text
cost ≈ C_bitstream + k · N_out
```

IDCT scaling shrinks `N_out`; it does not remove `C_bitstream`.

**Implication:** durable **tiles** on Store win on **repeat** open for PreferCache/TileSynth; cold archive JPEG still favors jpeg_shrink for first paint.

---

## 5. Archive member vs filesystem

| Step | Plain file | Archive (CBZ/CBR/…) |
|------|------------|---------------------|
| Locate bytes | open + read | open archive + find member (+ possible sequential scan) |
| Decompress container | — | deflate / RAR / … |
| Decode image | same codec | same codec after bytes are in RAM |

- ZIP/CBZ, indexed, light compression: often **~1.2–2×** vs loose file.
- Solid RAR / unindexed: can **dominate** everything; batch + interest windows matter more than codec choice.

---

## 6. Tiles: assemble vs one-shot (same final size)

**Need entire frame once, same pixel count, warm source:**

| Path | Relative |
|------|----------|
| One contiguous decode at that size | Baseline |
| Assemble full grid from tiles | typically **~1.1–1.5×** (extra calls, copies, locality) |

Tiles win when you need **less than the full frame**, pan/zoom, parallel cell decode, or partial cache hits — cost tracks **visible/requested cells**, not full \(W×H\).

Pipeline fit:

- First paint ≤1024 → one-shot soft/overview.
- Viewport ≤2048 with pyramid → PreferCache / TileSynth.
- Deep zoom → `request_tile` cells only.

---

## 7. Tile completeness (SQL) and interrupt / resume

Storage is **per cell** `(content_id, scale, x, y)` in index + blobs — not one atomic pyramid blob.

At scale `s`:

```text
tw = ceil(width  / 2^s / 256)
th = ceil(height / 2^s / 256)
```

**Complete at `s`** = all `tw×th` cells present.

| Check | Cost |
|-------|------|
| `get_tile_coverage` → `MIN/MAX(scale)` | **Very cheap** (does not prove full grid) |
| `COUNT(*)` for scale vs expected `tw×th` | **Cheap** |
| TileSynth (`get_pixels_from_tiles`) | Probes each cell via `get_tile`; any miss → incomplete (**moderate** SQL; decode/blit dominates if assembling) |

**Interrupt:** cells already upserted stay. Partial pyramids are normal.

**Continue:**

1. `request_tile(scale,x,y)` — hit or build **that cell only**.
2. Pyramid prewarm — should skip existing cells; resume = fill holes.
3. TileSynth — incomplete scale → miss; FocusFull / interest builds more tiles, then retry.
4. Soft ladder is independent of tile completeness.

Completeness checks are cheap next to encoding missing tiles. Avoid treating partial grids as Q2 full-frame (TileSynth requires every cell).

---

## 8. JPEG XL — where it helps

| Use | Role |
|-----|------|
| Lossy soft ladder, **low effort** | Durable 128–512; fast decode on repeat |
| Lossless JPEG recompression | Cache size; not first-open latency |
| Multi-thread decode | Large PreferCache / full frames |
| Tiles (or JXL region features) | Aligns with zoom without full framebuffer |
| High effort encode | **Not** on the interactive path |

JXL does **not** replace cold jpeg_shrink overview of a large archive JPEG for first paint; it replaces **repeat** extract+decode when soft exists.

---

## 9. Preferred time-to-pixels order

1. ImageCache / already painted  
2. Durable soft ≤512  
3. Overview ≤1024 (jpeg_shrink / FastScale)  
4. PreferCache / tiles ≤2048  
5. True full / native (settle shortfalls; no loop)

---

## 10. Constants (biltoo `ThumtooCache`)

| Name | Value | Role |
|------|-------|------|
| `kGalleryLadderEdge` | 512 | Durable soft max |
| `kBatchOverviewEdge` | 1024 | FastBatch overview cap |
| `kImageLadderEdge` | 2048 | PreferCache / display snap max |
| `ImageCache::kDisplayMaxEdge` | 2048 | Host RAM sample clamp |

---

*Session notes 2026-09-13: ladder semantics, progressive 1+4, scale-from-large vs native 1024, tile assemble cost, SQL completeness / resume.*
