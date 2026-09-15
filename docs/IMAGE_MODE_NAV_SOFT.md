# Image-mode ←/→ soft (LQIP / ladder) — state machine

This document is the **contract** for what should happen on session Next/Prev in
Image mode. Implementers and agents should read this before changing soft paths.

## Roles (do not conflate)

| Store | Key | Value | Lifetime |
|-------|-----|-------|----------|
| **ImageItem** sample | one live Image-mode item | `m_preview` (soft) or `m_source` (full) | Cleared on path change |
| **ImageCache** | decode **path** | raw ladder/LQIP/PreferCache (unoriented) | Process LRU (~384) |
| **Gallery stash** | path on stashed tiles | display sample (may be content-baked) | Survives Gallery→Image |
| **Filmstrip overrides** | SessionImageId | icon pixmap | Not path host — do not use as ImageCache |

`hasDecodedPixels()` = **full only** (`m_source` and not preview mode).  
`hasDisplayPixels()` = soft **or** full.

## Intended machine (happy path)

```
MainWindow::setCurrentIndex / goNext
  → applyCurrentIndexCanvasChange(path)
       setSlideshowNavHot(true)
       loadImage(path)
         setClassicPath(path)
         scheduleImageLoad(path, LoadReplace)
           installImageModePendingTile(path)     // SYNC, GUI, no IPC
             soft = ImageCache | slideshow | gallery stash
             path change → clearDisplayPixels (soft AND full)
             attach soft (setPreview / installDisplayPixels)
             setIntrinsicSize(layoutSize)
             viewport update
           if navHot: return                   // no PreferCache / classic decode
  → 80ms settle timer
       setSlideshowNavHot(false)
       loadImage(path) again                   // climb PreferCache / full
```

### Rules

1. **Key-repeat (nav hot):** only in-process soft swap + layout. No thumtoo IPC,
   no PreferCache, no classic full decode.
2. **Path change:** always clear **display** pixels (`hasDisplayPixels`), not only
   full (`hasDecodedPixels`). Leaving prior soft makes `canAcceptDisplaySample`
   reject a smaller LQIP for the next path.
3. **No high-res yet:** paint soft/LQIP (or blank placeholder at correct layout).
   Never keep the **previous path’s** sample.
4. **Layout without pixels is OK.** Wrong path’s pixels under a new contentRect is not.
5. **ImageCache** holds **raw** host samples. SoftPreview installs seed it.
   Stashed Gallery soft may already be content-baked → attach display-ready only
   (no put, no second materialize).
6. **Settle (~80ms quiet):** one PreferCache/full climb for the **current** path only.

## Failure modes (observed)

| Symptom | Cause |
|---------|--------|
| Blank on every ←/→ while Gallery shows soft | Prior soft not cleared (`hasDecodedPixels` only); `canAccept` rejects next LQIP |
| Stall under key-repeat | Sync `cachedLqipImage` / escalate under nav-hot |
| Stretch / wrong aspect | `drawImage(contentRect, sample)` with aspect mismatch (paint letterboxes when >3%) |
| Soft never in ImageCache | SoftPreview host put skipped when `wantBake` (fixed: SoftPreview always seeds raw) |

## Debug

```bash
BILTOO_LOAD_DEBUG=1   # or THUMTOO_DEBUG=1
# Look for: pendingTile path=… soft=WxH cache=0|1 displayReady=0|1
#           pendingTile INSTALLED …
#           PATH nav-hot soft-only …
```

If `soft=0x0` every time, ImageCache and stash both miss for that path (Gallery
never seeded host, or LRU evicted and no stash). If `soft=WxH` but still blank,
check `canAccept` / clear path and paint.

## Slideshow user ←/→ (related)

Slideshow does **not** call `loadImage` on user Next/Prev. It calls
`setSlideshowPhase(path, {}, -1)` which arms phase buffers and historically
scheduled **per keystroke**:

- dwell atlas rebuild (thread pool)
- phase-buffer content upgrade (thread pool)
- zoom-blur warm
- `preloadSlideshowImage` → PathRaster `EscalateToFull`

`m_slideshowNavHot` was set true on every user key and only cleared on
`stopSlideshow`, so heavy work never gated. Holding ←/→ flooded the pool and
PathRaster (progressively worse).

### Contract (aligned with Image-mode nav-hot)

| Phase | Work |
|-------|------|
| **Key-repeat (nav hot)** | Swap phase soft from ImageCache; **no** atlas / phase-upgrade / zoom-blur / PathRaster |
| **Settle (~80ms quiet)** | Clear nav-hot; `setSlideshowPhase` once → full dwell quality for **current** path |
| **Neighbour preload** | Still debounced (~200ms); skipped while nav-hot |

