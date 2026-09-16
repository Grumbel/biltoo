<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Thumtoo ↔ biltoo host contract

**Normative.** Agents and humans must read this before changing PreferCache,
soft climb, slideshow preload, or PathRasterService. Band widths and codecs:
[PERFORMANCE.md](PERFORMANCE.md). Climb ownership: [PATH_RASTER_SERVICE.md](PATH_RASTER_SERVICE.md).

This document is the **product rule**. Consumer-specific “retry once then full”
branches that contradict it are bugs.

---

## 1. Roles

| Layer | Owns | Does **not** own |
|-------|------|------------------|
| **Thumtoo** | Durable soft ladder, overview, PreferCache decode, tiles, full pixels; settle keys; `ladderReady` delivery. **`PixelSource::TileSynth` is a valid PreferCache/Overview delivery** (same `ladderReady` path as soft levels). SoftOnly stays soft-ladder-only (no TileSynth — filmstrip cost). | Host geometry, mode policy, which band a product surface needs |
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
| **Soft** | `schedulePixels` | ≤ **512** (`kGalleryLadderEdge`) | Soft ladder level ≤ request (durable) |
| **Overview** | `scheduleOverviewPixels` | ~**1024** (`kBatchOverviewEdge`) | jpeg_shrink / overview (not a soft level) |
| **Display (PreferCache)** | `scheduleDisplayPixels` | ≤ **8192** (`kImageLadderEdge`, interim) | **Best available ≤ request**: soft, overview, or tile reconstruct |
| **Full** | `scheduleFullPixels` | up to native / host max | Near-native / full decode path |

**Critical:** PreferCache does **not** guarantee `got ≥ 0.9 × requested`.
In current thumtoo, `request_raster(PreferCache)` with `max_edge > 512` is
routed to **overview** and **clamped to 1024** (`kBatchMaxEdge`). A host request
of 2048 therefore often returns **1024 TileSynth/overview** with `ok=0`. That is
thumtoo policy, not a biltoo install bug. Whole-frame samples above 1024 require
**Full** (`scheduleFullPixels`) or true per-cell tile paint.

Soft never stores 1024. Overview is not soft. PreferCache is not “force 2048.”

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
  SoftDisplay,    // Gallery: Soft → PreferCache; plateau is terminal for this want
  EscalateToFull, // Image mode + Slideshow: Soft → PreferCache → Full once
};
```

### SoftDisplay (Gallery)

1. Soft if `have == 0`
2. PreferCache (effectively ≤1024 overview/TileSynth) until Met or BestAvailable
3. On BestAvailable while `want` > overview (~1024): **one Full** for that path
   (PreferCache cannot deliver 2048). Gallery only `ensure`s a bounded visible set
4. Avoid `setInterest` on every column pack — debounce decode-window/interest

### EscalateToFull (Image mode, Slideshow)

1. Soft if `have == 0`
2. PreferCache (≤1024 effective) until Met or BestAvailable
3. On BestAvailable while `want` > overview: **one Full** (required for >1024)
4. Optional FocusFull if Full fails to queue
5. Full exhausted → terminal for this want (raise want clears latches)

Raising `want` past `lastDisplayWant` clears the PreferCache plateau latch so a
higher band can be requested (gallery zoom, Image zoom).

---

## 5. Consumer need edges (product)

| Consumer | Target / need | Climb policy |
|----------|---------------|--------------|
| **Gallery** | On-screen cell long edge (ladder-snapped) | SoftDisplay |
| **Image mode** | Viewport × DPR (capped), then native if still short | EscalateToFull |
| **Slideshow** | `ladder(viewport × DPR × motionHeadroom)` capped at kImageLadderEdge; **need** ≈ 70% of target | EscalateToFull |

Gallery **`GallerySoftState`** is prioritization only (concurrency, blank tiles,
on-screen want). PreferCache plateau is mirrored from PathRasterService — not
decided in `noteLadderDelivery`.

Slideshow geometry is **logical size**, not sample size ([SLIDESHOW.md](../SLIDESHOW.md)).
Sample climb only changes sharpness.

---

## 6. Forbidden host patterns

1. **Per-tick `ensure` spam** — look-ahead preload at most once per slideshow
   `toIdx` (or equivalent), not every clock tick.
2. **Consumer PreferCache retry** after BestAvailable (`forgetPixelsSettled` +
   `clearPreferGaveUp` as a product feature). Retry belongs only inside the
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
| `ThumtooCache::scheduleOverviewPixels` | Overview | Optional explicit overview |
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
| Gallery dual scrollbars / soft stuck | Separate issues; SoftDisplay + raise-want on zoom | Do not Full-escalate gallery |
| Full request loops | Shortfall not settled | Thumtoo settle + host one-shot Full flag |

---

## 9. Change control

- Changing band edges or PreferCache guarantees requires updating **this file**
  and PERFORMANCE.md in the **same** tip.
- New consumers must pick SoftDisplay or EscalateToFull; do not invent a third
  recovery path in ImageView.
- Thumtoo-side API changes should land with a biltoo tip that adjusts this
  contract before consumer workarounds.

