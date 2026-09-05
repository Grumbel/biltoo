# Slideshow invariants and known failure modes

Read this before changing live transitions, preload, handoff, motion, or the
pure-clock scheduler. Most “glitches” in this area are regressions of the same
mistakes.

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
