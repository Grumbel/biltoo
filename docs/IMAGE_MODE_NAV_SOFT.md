# Image-mode ←/→ stand-in (LQIP / tiles) — state machine

This document is the **contract** for session Next/Prev in Image mode.
Implementers and agents must read it before changing underlay, probe, or tile
paths. Soft PreferCache / soft-ladder whole-frame encode for Image underlay is
**removed** (same product decision as Gallery — see [GALLERY_SOFT.md](GALLERY_SOFT.md),
[GALLERY_PIXELS.md](GALLERY_PIXELS.md), [THUMTOO_HOST_CONTRACT.md](THUMTOO_HOST_CONTRACT.md)).

## Product layers (do not invent soft)

| Layer | Role | When it appears |
|-------|------|-----------------|
| **Placeholder** | Gray box in `contentRect` | Always if no display sample yet |
| **Sized placeholder** | Same, intrinsic = layout size from size memo/probe | Size known (memo / prior probe) |
| **LQIP** (≤96) | Cheap underlay texture | **Only** if already in process/Store cache — free side-effect of prior **tile** (or historical soft) work. **Never** `request_lqip` / standalone LQIP encode |
| **Tiles** | Sharpness | Issued after dwell/settle; durable Store hits preferred |
| **Filmstrip / ImageCache host** | In-process stand-in (any edge) | If already decoded for strip or prior view — not a soft ladder request |

**Size probe** answers **geometry only**. A size reply may *include* a cached LQIP
blob when thumtoo already has one; the probe does **not** generate LQIP.

## Roles

| Store | Key | Value | Lifetime |
|-------|-----|-------|----------|
| **ImageItem** sample | one live Image-mode item | `m_preview` (LQIP/host underlay) or `m_source` (full) | Cleared on path change |
| **ImageCache** | decode **path** | raw host (LQIP, filmstrip thumb, PreferCache/TileSynth if any) | Process LRU (~384) |
| **Gallery stash** | path on stashed tiles | display sample (may be content-baked) | Survives Gallery→Image |
| **Filmstrip** | row / SessionImageId | icon pixmap | Prefer via soft provider; may re-seed ImageCache |

`hasDecodedPixels()` = **full only**.  
`hasDisplayPixels()` = underlay **or** full.

## Intended machine (happy path)

```
MainWindow::setCurrentIndex / goNext
  → applyCurrentIndexCanvasChange(path)
       setSlideshowNavHot(true)
       loadImage(path)
         setClassicPath(path)
         scheduleImageLoad(path, LoadReplace)
           installImageModePendingTile(path)     // SYNC, GUI, no encode IPC
             host = ImageCache | filmstrip | gallery stash | cached LQIP
             path change → clearDisplayPixels
             if host: attach SoftPreview-kind underlay + layout
             else: setIntrinsicSize(layoutSize) → paint draws placeholder
           if navHot: return                   // no probe flood, no tiles, no climb
  → 80ms settle timer
       setSlideshowNavHot(false)
       loadImage(path) again                   // size probe if needed + tiles
```

### Rules

1. **Key-repeat (nav hot):** in-process underlay swap + layout only. No thumtoo
   encode IPC, no PreferCache climb, no classic full decode, **no tile
   plan/issue/paint**.
2. **Path change:** always clear **display** pixels (`hasDisplayPixels`). Leaving
   prior underlay makes `canAcceptDisplaySample` reject a smaller LQIP for the
   next path.
3. **Display priority while hot:**
   - in-process LQIP / filmstrip host / stash if present → paint it;
   - else sized placeholder (size memo known) or provisional placeholder;
   - **never** generate LQIP or soft ladder to fill the blank during the burst.
4. **Layout without pixels is OK.** Wrong path’s pixels under a new contentRect is not.
5. **Image mode paint underlay:** while tiles do not fully cover, interactive
   Image/Workspace may draw any in-process host (including filmstrip >96).
   Gallery remains **LQIP-only** under `tileLodWanted` (soft removed).
6. **Settle (~80ms quiet):** size probe if needed; tile issue for current path.
   No `scheduleSoftPixels` for Image underlay.
7. **Cold blank under nav-hot:** no `scheduleProbe` / soft encode per key (queue
   flood / stall). Placeholder is correct until settle.
8. **Nav-hot materialize:** content bake uses a tighter clamp (≤256) and never
   schedules async rematerialize for skipped paths. Entering nav-hot drops
   neighbor tile-prefetch slots.
9. **`driveImageFocusSurface` / DisplaySurface actions:** no-op under nav-hot
   (must not `pathRaster->ensure` every skipped path).

## Failure modes (observed)

| Symptom | Cause |
|---------|--------|
| Blank on every ←/→ with hot ImageCache | Path not cleared; or Image paint treated Gallery “LQIP-only under tiles” and suppressed filmstrip host while nav-hot skipped tile paint |
| Stall under key-repeat | Soft encode / tile plan under nav-hot; sync materialize of large hosts |
| Skip frames while holding ←/→ | Sync `repaint()` every key (use `update()` when nav-hot) |
| Stretch / wrong aspect | Prior path pixels under new contentRect |
| “Generate LQIP on blank” | Forbidden — LQIP is cache-only free data from tile work |

## Debug

```bash
BILTOO_LOAD_DEBUG=1   # or THUMTOO_DEBUG=1
# Look for: pendingTile path=… soft=WxH cache=0|1 displayReady=0|1
#           pendingTile INSTALLED …
#           pendingTile DEFER blank … placeholder
```

If `soft=0x0` every time, no in-process host for that path (warm memos never
seeded LQIP, filmstrip not decoded, LRU miss). Placeholder is correct until
settle tiles/LQIP-from-prior-tile-work appear. If `soft=WxH` but still blank,
check Image paint underlay vs Gallery LQIP-only rule.

## Slideshow user ←/→ (related)

Slideshow does **not** call `loadImage` on user Next/Prev. It calls
`setSlideshowPhase(path, {}, -1)`.

### Contract (aligned with Image-mode nav-hot)

| Phase | Work |
|-------|------|
| **Key-repeat (nav hot)** | Swap phase stand-in from ImageCache; **no** atlas / phase-upgrade / zoom-blur / PathRaster |
| **Settle (~80ms quiet)** | Clear nav-hot; `setSlideshowPhase` once for **current** path |
| **Neighbour preload** | Debounced; skipped while nav-hot |
