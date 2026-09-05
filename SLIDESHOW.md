# Slideshow — pure model (read this before changing code)

## Interval and transition (wall-clock arithmetic)

```
intervalMs  = pureMs + transitionMs     // e.g. 1000 + 500 = 1500
cycle       = floor(wallMs / intervalMs)
phaseMs     = wallMs % intervalMs       // 0 .. intervalMs-1

if phaseMs < pureMs:
    // DWELL slide[cycle]
    opacityFrom = 1
    opacityTo   = 0
    fromIdx     = cycle % n
    toIdx       = unused
else:
    // CROSSFADE slide[cycle] → slide[cycle+1]
    t           = (phaseMs - pureMs) / transitionMs   // 0..1
    opacityFrom = 1 - t
    opacityTo   = t
    fromIdx     = cycle % n
    toIdx       = (cycle + 1) % n
```

One pure clock in MainWindow owns `wallMs`. ImageView does **not** schedule
advances. ImageView only: (1) ensure pixel buffers for from/to, (2) blit.

## Pixels for a path

```
buf(path) = full_decode if ready
         else cache_preview scaled to native size (same aspect, same cover math)
```

Full may replace soft later; geometry must not jump.

## What to paint (pure function of the numbers above)

```
DWELL:     blit(buf(fromIdx), motionT=f(path, wall), opacity=1)
CROSSFADE: blit(buf(fromIdx), motionT_from, opacity=1-t)
           blit(buf(toIdx),   motionT_to=0.., opacity=t)
```

Ken Burns is optional sampling on a buffer: `motionT = clamp(localMs / pathMs, 0, 1)`,
**restarted at 0 when that path becomes the dwell slide**. No second clock for
scheduling. Underlay QGraphicsItem is **never shown** during the show.

## What is forbidden

- Events that cancel motion / clear the canvas during the show (pending tile)
- Soft-handoff that waits on LoadReplace to install the dwell buffer
- A second scheduler (single-shot timers, resume chains)
- Showing the underlay “for one frame” during handoff
- Carrying motion progress across different images until t=1 freezes

## Implementation map

| Concern | Where |
|---------|--------|
| wallMs, cycle, phase, which indices | MainWindow pure clock |
| buf(path), beginLive when phase enters fade | ImageView buffers |
| blit | ImageView paint only |
| LoadReplace | updates hidden underlay only; never cancels motion during show |

---

## Settled design (do not grow a second scheduler)

| Concern | Owner |
|---------|--------|
| When / which slide | `MainWindow` pure wall clock only |
| How pixels crossfade | `ImageView` live blit (opacity + two frames) |
| Ken Burns per image | One path 0→1 over `interval+transition`; restart at 0 on each new slide |
| Underlay QGraphicsItem | **Hidden** for the whole slideshow session |

Defensive offset math, dual wall clocks, and “continue progress across images”
are what produced the paint-log jumps. Prefer the table above over another patch.

## Architecture (do not invent a second scheduler)

| Piece | Owns |
|-------|------|
| `MainWindow` pure clock (`updateSlideshowFromClock`) | *When* and *which* pair `(fromIdx → toIdx)` |
| `ImageView` live/snapshot transition | *How* the pixels crossfade / slide |
| `slideshowLiveTransitionFinished` | Commit `setCurrentIndex(to)` only — never schedule the next step |
| `m_handoffPath` / `m_handoffImage` | Full-res pixels for `LoadReplace` so goNext does not re-decode |
| `m_preloadPath` / `m_preloadImage` | Next full-res decode before the transition window |
| `ImageCache` | Process-wide previews (≥512 long-edge); **paint aid only** |

The wall clock is the only schedule authority. ImageView must not arm the next
advance from finished/hold/dwell signals.

## Hard rules (break these → jumps / stuck / low-res lock-in)

### 1. Live “to” frame geometry must match native full size

Motion cover uses **absolute pixel size** for cover scale and travel thresholds.
A 512×341 “to” against a 3744×5616 “from” always jumps when full-res lands.

- Full preload hit → use full pixels.
- Cache preview → **scale up to native size** (`m_imageSizeByPath` or
  `ImageLoader::probeSize`) before `startLiveTransitionWithImage`.
- Never paint a raw thumbnail as the live “to” layer.

### 2. Never write thumbnails into `m_handoff*`

`LoadReplace` treats handoff as a completed full decode. A thumb in handoff
locks Image mode onto low-res until a later accidental reload.

Handoff = full decode only. Cache-hit paint path must leave handoff empty so
`LoadReplace` still runs a real full load (or a later full preload).

### 3. Do not cancel in-flight preload by bumping generation on the same path

`preloadSlideshowImage` must no-op when:

- `path == m_preloadPath` and image ready, or
- `path == m_preloadInFlightPath`, or
- `path == m_handoffPath` and handoff still holds pixels.

Bumping `m_preloadGeneration` on every call cancelled the job that beginLive was
waiting on and forced every cycle down the full-decode path (phase drift).

### 4. Do not re-preload the transition *target* while it is the handoff

After `beginLive` preload-hit moves pixels into handoff and clears
`m_preload*`, the tick must **not** start another full disk load of the same
path. Warm the *next* index (`(toIdx+1) % n`) while a transition is busy.

### 5. Busy must not skip the pure clock forever

If `isSlideshowTransitionBusy()` and `cycle > m_slideshowTransitionCycle`,
cancel the stale overlay and start the transition for the **current** cycle.
Otherwise superfast / `interval == transition` (pureMs=0) locks until stop.

### 6. Soft handoff before dropping live flags

At end of live fade: install dwell cover (biases + source + atlas + progress)
**before** clearing `m_liveTransition*`. One exposed frame of the fitted
QGraphics underlay is a full-screen flash.

### 7. `maybeStartSlideshowMotion` must no-op during live / hold / awaiting

`startSlideshowMotion` cancels the motion timer. Calling it mid-crossfade from
`onImageLoaded` freezes or resets Ken Burns under the overlay.

### 8. Motion travel thresholds must be resolution-relative

Fixed pixel thresholds (e.g. `kMinTravel = 8`) make PanScan zoom differently
for 512 vs 5000 px frames. Use a fraction of the long edge.

### 9. Pause is a session state, not a 1s flash

`flashHud` times out. Paused slideshow needs a **persistent** top-left cue
(`setSlideshowPausedHud`) until resume/stop.

### 10. Pure clock vs interval changes

Changing interval while running must `cancelSlideshowTransition`, reset
`m_slideshowTransitionCycle` / pending index, and re-arm. Leaving busy=true
with a reset cycle made the pure clock wait forever.


### 11. Never install a pending Image-mode tile under a live hold

`installImageModePendingTile` clears the canvas and cancels motion. Under an
active live fade/hold that is a hard jump (placeholder framing). Skip entirely
when `m_liveTransitionActive|Hold|AwaitingLoad`. Full pixels arrive via
`onImageLoaded` → soft handoff.

### 12. Soft cache-hit must upgrade in place when full preload lands

Cache-hit paints a native-size upscaled preview and must still run full preload.
When `preload-ready` matches `m_liveTransitionNextPath` during live, replace
`m_liveTransitionSourceImage` + set `m_handoff*` + rebuild atlas — do not restart
the fade. Sharpness improves; geometry stays put.


### 13. Never discard a ready or needed in-flight preload

`preloadSlideshowImage` must **not**:

- `clear()` a READY `m_preload*` of a *different* path (tick warming ahead
  wiped the image beginLive was about to use),
- bump generation to cancel an in-flight decode of `m_liveTransitionNextPath`
  or `m_handoffPath` (soft cache-hit never got `live-upgrade`).

Only one ready slot exists. Warm the *next* path only after the current ready
slot has been consumed into handoff/beginLive.


### 14. One Ken Burns path per image

- **From** layer keeps the dwell wall clock through the fade.
- **To** layer starts its path at 0 (`toLayerWallMs = wallMs`).
- **Soft-handoff** starts a **new** path at 0 for the new slide (do not carry
  mid-path progress across images — that freezes at dwellT=1.0 on the next fade).


### 15. Underlay is never visible while slideshow progress is active

`setSlideshowUnderlayVisible(true)` is ignored during the show. Only dwell/live
blits draw. Cancel/handoff must not flash `mode=underlay` with an empty item.

## Symptoms → usual cause

| Symptom | Usual cause |
|---------|-------------|
| Jump when new slide “sharpens” | Thumb/cache used as live “to” or handoff without native-size scale |
| Stuck low-res forever | Thumb written to `m_handoff*` |
| Phase drift / every cycle `full-decode` | Preload generation cancelled in-flight; or pureMs=0 never warmed next |
| Superfast freezes / skips | Busy early-return without cycle catch-up |
| One-frame fullscreen flash | Hold dropped before dwell cover installed |
| Motion freezes mid-fade | `startSlideshowMotion` during live |
| Pause looks like stop | Only `flashHud("Paused")`, no permanent HUD |
| Same file decoded twice per cycle | Tick re-preloads `toIdx` after handoff consumed preload |

## Debug logs

`[slideshow]` lines (keep them):

- `start-transition cycle=… pureMs=… trMs=… intervalMs=… from=… to=…`
- `beginLive preload-hit|cache-hit|wait-preload|request-preload`
- `preload-start` / `preload-ready`

A healthy steady state after the first cycle is mostly **preload-hit** with
matching `from=` / paint sizes. Repeated `preload-start` of the **same** file
immediately after a hit is a regression of rule 4.

## What is *not* a bug by itself

- Portrait ↔ landscape crossfade changes cover crop; that is aspect change, not
  a size mismatch. Still requires both frames at native full size.
- Soft (upscaled preview) pixels during a cache-hit fade are expected; geometry
  must not jump when full-res replaces them.
