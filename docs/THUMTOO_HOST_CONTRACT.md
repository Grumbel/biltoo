<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Thumtoo ↔ biltoo host contract

**Normative.** Agents and humans must read this before changing PreferCache,
soft climb, slideshow preload, or PathRasterService. Band widths and codecs:
[PERFORMANCE.md](PERFORMANCE.md). Climb ownership: [PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md).

**Pixel durability (thumtoo):** soft whole-image is **ephemeral** (or TileSynth); durable multi-res is **tiles** + **LQIP** only. See thumtoo `docs/PIXEL_AND_ARCHIVE_POLICY.md`. Biltoo should schedule tiles for display when possible.

**Warm cache rule:** if `ImageCache` already covers the requested edge, host must
not call PreferCache / soft encode / thread-pool work for that path. Store durable
tiles → skip soft underlay; tile LOD owns paint.

**LQIP (host rule):** never call `request_lqip` / `ensure_lqip` to generate placeholders.
LQIP is low utility and must only appear when thumtoo already encoded it for free
during tile (or soft) work. `get_lqip` / size-reply LQIP is cache-only.

This document is the **product rule**. Consumer-specific “retry once then full”
branches that contradict it are bugs.

---

## 1. Roles

| Layer | Owns | Does **not** own |
|-------|------|------------------|
| **Thumtoo** | PreferCache **TileSynth**, **durable tiles + LQIP**, overview/full when needed; settle keys; `ladderReady` delivery. **`PixelSource::TileSynth` is a valid PreferCache delivery**. Host product underlay is LQIP + tiles — not SoftOnly encode. | Host geometry, mode policy, which band a product surface needs |
| **ImageCache** | Process RAM path → best raw sample (upward-only, ≤ display max) | Scheduling |
| **PathRasterService** | Per-path want / have / climb band / escalate policy; the **only** host scheduler of soft → PreferCache → (optional) full | Paint, phase buffers, gallery prioritization |
| **ImageView / consumers** | Need edge (viewport, zoom, slideshow headroom); install into items / phase buffers | Direct PreferCache re-queue after shortfall; inventing a second climb |

Consumers call `PathRasterService::ensure` (with a climb policy). They **must not**
call `scheduleDisplayPixels` / `forgetPixelsSettled` / ad-hoc PreferCache retries
to “unstick” a path. That is the service’s job under the policy below.

**ImageLoader exception:** `ImageLoader` may schedule **Soft** (≤512) and
**Overview** (≤1024) for `loadThumbnail` / filmstrip / page soft stand-ins when
PathRaster is not the caller. It must **never** `schedulePixels` above soft max
and must **not** call PreferCache Display or Full.

Display (PreferCache) climb goes through PathRasterService. Full for display
escalate is PathRaster `EscalateToFull`. Full for **crop/edit native** is
`requestCropFullRaster` / `scheduleFullPixels` (edit path, not display climb).

ImageView LoadReplace cold open uses `requestEscalateClimb` (not bare
`schedulePixels`).

---

## 2. Request bands (what a schedule means)

Biltoo requests a **band**, not “exactly N pixels.” Edge numbers snap via
`ceilLadderEdge` and are capped by known native when available.

| Band | API (host) | Typical long edge | Thumtoo may return |
|------|------------|-------------------|--------------------|
| **LQIP** | cache / size-reply only | ≤ **96** | Free side-effect of prior tile work — **never** `request_lqip` encode |
| **Display (TileSynth)** | `scheduleDisplayPixels` **only if** `hasDurableTilesKnown` | ≤ **8192** (`kImageLadderEdge`, interim) | PreferCache **TileSynth** from durable tiles |
| **Cold tiles** | `scheduleTilePyramid` | (pyramid) | Builds durable coverage; then TileSynth |
| **Full** | `scheduleFullPixels` | up to native / host max | Near-native / full decode path |

**Product underlay (Gallery, Image, filmstrip, PathRaster soft-band):** LQIP +
**tiles** only. Use **`scheduleTileSynthOrPyramid`** (TileSynth when durable
tiles known, else pyramid). Do **not** call bare `scheduleDisplayPixels` or
`scheduleSoftPixels` without a tiles-known guard — PreferCache still
soft-encodes when no pyramid. Soft PreferCache encode is **removed** from host
product paths (biltoo ≥1220; helper ≥1227).

**Critical:** PreferCache does **not** guarantee `got ≥ 0.9 × requested`.
In current thumtoo, `request_raster(PreferCache)` with `max_edge > 512` is
routed to **overview** and **clamped to 1024** (`kBatchMaxEdge`). A host request
of 2048 therefore often returns **1024 TileSynth/overview** with `ok=0`. That is
thumtoo policy, not a biltoo install bug. Whole-frame samples above 1024 require
**Full** (`scheduleFullPixels`) or true per-cell tile paint.

Soft PreferCache encode is not a product underlay. Overview is not soft.
PreferCache is not “force 2048.” Use TileSynth when durable tiles exist.

---

## 3. Delivery outcomes (host interpretation)

Every completion that reaches `PathRasterService::noteDelivery` is classified:

| Outcome | Rule (host) | Effect on climb |
|---------|-------------|-----------------|
| **Met** | `got ≥ ~90%` of the **request** edge for that job | Update `have`; continue only if `have` still short of **want** |
| **BestAvailable** | `got > 0` but `got < ~90%` of request (PreferCache / overview plateau) | Latch PreferCache plateau for this **want**; do **not** spin the same Display request |
| **Failed** | null / zero pixels | Do not mark Met; may retry under separate failure policy |

`have` is always `ImageCache` long edge (upward-only). Want is the consumer’s
capped target edge.

**Adequacy for product UI** (slideshow need ~70% of target, gallery on-screen
edge, etc.) is **stricter or looser** than Met. Met is about the **request**;
need is about the **viewport**. PreferCache can be Met for an overview-sized
request and still fail slideshow need if want was 2048 and only 1024 exists.

---

## 4. Climb policies (PathRasterService)

```text
enum class ClimbPolicy {
  SoftDisplay,    // Soft → PreferCache/TileSynth ≤ overview; no Full native
  EscalateToFull, // Soft → PreferCache → Full once (Image mode cold path)
};
```

### SoftDisplay

Used by **filmstrip**, **Workspace** (non-focus), and **Slideshow** (screen-fit).

1. Soft if `have == 0`
2. PreferCache / TileSynth until Met or BestAvailable (overview band)
3. Plateau is terminal for this want — **no Full native**

### Gallery (no PathRaster soft climb)

Gallery does **not** call `PathRasterService::ensure` for underlay. Pixels:

- **LQIP** placeholder (ImageCache / Store)
- **Tiles** via TileLoadCoordinator + shared `TileLodRegistry` path cache

See [GALLERY_PIXELS.md](GALLERY_PIXELS.md). Soft PreferCache underlay is removed
([GALLERY_SOFT.md](GALLERY_SOFT.md)).

### EscalateToFull (Image mode, cold)

1. Soft if `have == 0`
2. PreferCache until Met or BestAvailable
3. On BestAvailable while `want` > overview: **one Full** when no durable tiles
4. When durable tiles exist, Image mode uses **tiles** instead of Full

Slideshow uses SoftDisplay only (screen-fit edge), never EscalateToFull.

---

## 5. Consumer need edges (product)

| Consumer | Target / need | Climb policy |
|----------|---------------|--------------|
| **Gallery** | *(not PathRaster)* | LQIP + tiles only |
| **Image mode** | Viewport × DPR (capped), then native if still short | EscalateToFull |
| **Slideshow** | `ladder(viewport × DPR × motionHeadroom)` capped at kImageLadderEdge; **need** ≈ 70% of target | EscalateToFull |

Gallery **`GallerySoftState`** is decode-window bookkeeping only (concurrency, blank tiles,
on-screen want). PreferCache plateau is mirrored from PathRasterService — not
decided in `noteLadderDelivery`.

Slideshow geometry is **logical size**, not sample size ([SLIDESHOW.md](../SLIDESHOW.md)).
Sample climb only changes sharpness.

---

## 6. Forbidden host patterns

1. **Per-tick `ensure` spam** — look-ahead preload at most once per slideshow
   `toIdx` (or equivalent), not every clock tick.
2. **Consumer PreferCache retry** after BestAvailable (`forgetPixelsSettled`). Consumer PreferCache retry is not a product
   feature). Retry belongs only inside the
   service if the contract is extended; today plateau → Full under
   EscalateToFull, or raise want under SoftDisplay.
2b. **Second Full / PreferCache owners** outside PathRasterService (including
   pool workers). Workers must `requestEscalateClimb` on the GUI thread.
   Soft-only `schedulePixels` from ImageLoader is allowed (§1 exception).
3. **Assuming PreferCache returns want** — log/HUD may show target 2048 while
   have is 1024; that is plateau, not a silent bug by itself.
4. **Second climb owners** — no parallel ImageModeClimb / gallery pool PreferCache.
5. **Sample size as geometry** — [SIZE.md](../SIZE.md).

---

## 7. API map (implementation checklist)

| Host call | Band | Notes |
|-----------|------|--------|
| `ThumtooCache::schedulePixels` | Soft | Settled on success; skip if inflight/settled |
| `ThumtooCache::scheduleDisplayPixels` | Display | PreferCache; BestAvailable common |
| `ThumtooCache::scheduleFullPixels` | Full | Image / slideshow escalation |
| `Bridge::ladderReady(path, requestEdge, image)` | any | Always `noteDelivery` first |
| `PathRasterService::ensure(path, want, native, policy)` | climb | Idempotent |
| `ImageView::requestEscalateClimb(path, want)` | climb | GUI-safe wrapper: EscalateToFull ensure (pool workers queue this) |
| `PathRasterService::isGaveUp` | — | PreferCache plateau for current want |
| `PathRasterService::isClimbPending` | — | Soft, Display, or Full queued |

Provenance (`ladderProvenance` / pixel source) should be treated as diagnostic
until wired into delivery status; do not branch product logic on source alone
without documenting it here.

---

## 8. Observed failure modes (mapped)

| Symptom | Real cause | Correct response |
|---------|------------|------------------|
| Slideshow soft forever, want 2048, have 1024 | PreferCache BestAvailable; no Full escalate | EscalateToFull policy on ensure |
| `preload-ensure` log storm | Clock called ensure every tick | Once-per-toIdx gate + quiet early-out when pending/adequate/plateau handled |
| Gallery dual scrollbars / blank cells | LQIP install + tile coordinator budget | Do not reintroduce PreferCache soft |
| Full request loops | Shortfall not settled | Thumtoo settle + host one-shot Full flag |

---

## 9. Change control

- Changing band edges or PreferCache guarantees requires updating **this file**
  and PERFORMANCE.md in the **same** tip.
- New consumers must pick SoftDisplay or EscalateToFull; do not invent a third
  recovery path in ImageView.
- Thumtoo-side API changes should land with a biltoo tip that adjusts this
  contract before consumer workarounds.

