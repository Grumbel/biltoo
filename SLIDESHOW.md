# Slideshow

## Timeline

One continuous wall clock. For each slide **A**, then **B**, then **C**, …:

```
[---- interval − transition : A only ----][--- transition : A + B ---]
[---- interval − transition : B only ----][--- transition : B + C ---]
[---- interval − transition : C only ----][--- transition : C + D ---]
 …
```

| Name | Meaning |
|------|---------|
| **interval** | Time from the start of one slide’s “only” period to the start of the next slide’s “only” period |
| **transition** | Length of the A+B (or B+C, …) overlap at the end of that interval |
| **dwell** | The “only” part: `interval − transition` |

Each slide is on screen for a full **interval** of wall time: first alone, then sharing the screen during the transition, then the next slide continues alone.

## What is on screen

### Dwell (A only)

- Draw **A** at full opacity.
- Dwell motion (Ken Burns / pan-zoom) runs on **A**.

### Transition (A + B)

- Draw **A** and **B**.
- **Opacity** is what the transition controls. Motion keeps running on both.
- **Crossfade:** A opacity 1→0, B opacity 0→1. At the midpoint B is dominant; at the end B is the only image.
- **Fade-to-black:** V-shaped envelope. First half: only A, fading toward black (at midpoint the screen is black). Second half: only B, rising from black to full. A and B switch roles at the midpoint; they are not both visible as a blend over black.

After the transition, the next dwell is **B only**, with the same rules.

### Slide (projector)

- Geometry, not opacity: **A** translates left off-screen; **B** enters from the right.
- Same pure wall clock and dual motion as crossfade; only the composite differs.
- Implemented on the pure phase path (`setSlideshowPhase` + paint translates).

## Motion

Every image that is part of the current segment is in motion for as long as it participates:

- **A** moves for its full interval (dwell + transition).
- **B** moves for the whole transition *and* its following interval (not only after the transition ends).

Motion is independent of opacity. The transition changes compositing, not whether motion runs.

When the transition ends and dwell becomes **B**, **promote** B’s motion clocks and
**to-atlas → dwell atlas** so Ken Burns continues without restarting at 0 and
without a multi-MP `drawImage` hitch while a new atlas rebuilds.

## Pixels

For any path on screen, **each draw** chooses:

```
if better raster ready for path → blit it
else                            → blit soft placeholder
```

No events in the draw path. Decode is started ahead of time and stored as a
pollable buffer. When better pixels arrive, the **next** draw uses them.

**Logical size owns geometry.**

| Concept | Source | Role |
|---------|--------|------|
| **Logical size** | `slideshowLogicalSize` / `ensureSlideshowLogicalSize` (`m_imageSizeByPath`, thumtoo, probe) | Fit, fill, actual, Ken Burns, travel |
| **Sample raster** | `ImageCache` via `putSlideshowRaster` / `slideshowRaster` (target-edge) | Sampling only |
| **Phase buffers** | `m_ssFromImage` / `m_ssToImage` (+ oriented copies) | What paint samples |
| **Motion atlas** | `m_dwellAtlas` / `m_ssToAtlas` (viewport × headroom long edge) | Cheap per-frame blit |

Soft and target-edge placeholders are treated like the real image for geometry.
While size is still **provisional** (square archive stand-in before probe),
`resolveMotionLogicalSize` uses the **sample aspect** so dest is not a
1:1 box stretching a landscape soft (speed change / seek).
Never derive camera math from sample pixel width/height. Never write sample
dimensions into the logical size map. Phase entry calls `ensureSlideshowLogicalSize`; paint and static framing use
`slideshowZoomBaseScale(logical, viewport)` so Fit/Fill/Actual stay consistent
between the pure-phase blit and the underlay camera.

**Decode target** (long edge):

```
ladder(ceil(viewportLong × DPR × motionHeadroom))
```

capped at the image ladder max (2048). Headroom covers Ken Burns zoom past
1:1 cover. This is not “max / native.”

### Phase-buffer quality climb

- Any **sharper** sample may replace the phase buffer (`phaseBufferWantsSample`).
- **Atlas rebuild** is throttled: only when the atlas is empty, coverage fails,
  or the sample reaches the slideshow **need edge** (~70% of target), so
  intermediate soft→soft-ish steps do not thrash the GUI.
- PreferCache is re-queued until the host sample meets the need edge (soft-only
  preload must not stop the climb).
- **Shortfall recovery:** PathRasterService latches `preferGaveUp` when PreferCache
  returns below ~90% of the requested edge. Slideshow want is fixed for a dwell,
  so that latch would leave soft on screen forever. Recovery:
  1. one PreferCache retry (`clearPreferGaveUp` + `forgetPixelsSettled`);
  2. for active phase paths (from/to) only, quiet `scheduleFullPixels` (same
     idea as Image-mode native full after PreferCache exhausts).
- Look-ahead preload (`toIdx`…`+3`) runs **once per toIdx**, not every clock tick.
- Clamp + orient run on the **thread pool**; the GUI only assigns the finished
  buffer.
- Do **not** call Image-mode `loadImage` / `LoadReplace` while a slideshow
  session is active. The pure-phase path owns the viewport; racing underlay
  PreferCache after `phase-from` already at target edge caused frame drops.

### Motion atlas quality

- Atlas size is **viewport-driven** (longCap), not source-driven.
- Rebuild on the pool with **`Qt::SmoothTransformation`** (never Fast upscale of
  soft samples — that looked nearest-neighbour for the whole dwell).
- Soft-upscaled atlases do **not** count as covering PreferCache HQ
  (`srcLong ≤ atlasSize` was wrong — soft is stretched to longCap).
- Keep the previous atlas until the new one finishes (`finishSlideshowAtlas`
  swaps in place). On promote, transfer the to-atlas to the dwell atlas.
- `paintMotionCover`: prefer the atlas with `SmoothPixmapTransform`; if falling
  back to `drawImage`, enable smooth unless the sample is huge.

**Letterbox ZoomBlur** is a separate low-res underlay cache keyed by
path + viewport only (not source dimensions — soft→HQ must not miss the cache).
Keep the previous underlay until the new key is ready; never discard it on
navigation just because the new blur has not finished.

**Pause exception:** while paused the clock (and usually redraws) stop. If
sharper pixels arrive for the visible path, schedule **one redraw**. That is
only “pixels ready → update view,” not transition start/cancel/hold logic.

Do not wire “decode finished” into start/cancel/hold of the pure wall clock.

## Scheduling

A single wall-clock phase decides the segment (dwell vs transition) and which pair (A, B). Rendering draws the buffers for that phase. There is no second scheduler.

The pure-phase path (`setSlideshowPhase`) is the only transition implementation
for Crossfade, Fade-to-black, Slide, and None (hard cut). The older live
dual-blit / snapshot overlay path is retired.

### Auto-advance

When the clock commits a new slide, `setCurrentIndex` runs with
`m_slideshowAdvancing`. That must **not** call `loadImage`. Phase-from /
promote already hold the pixels.

### User navigation (← / →)

`onSlideshowUserNavigated` resets the pure clock base and calls
`setSlideshowPhase` for the navigated path. Neighbour preload is debounced.

While the slideshow session is active, `applyCurrentIndexCanvasChange` does not
`loadImage` (same rule as auto-advance).

## Leaving the show (Esc / stop)

Slideshow advances `m_currentIndex` without `loadImage` (phase buffers own the
viewport). On stop, **reload the current path** into Image mode after
`setSlideshowProgress(false)` so the canvas matches the slide that was showing
— not the image that was open when the show started.

## Pause

While paused, the clock does not advance. The pause state stays visible until resume or stop.

### User navigation while paused

← / → (and filmstrip) must still show the new slide. The pure phase buffers are
updated immediately (`setSlideshowPhase` to the current path). Unpausing must
not be required to see the navigated image.

## Image mode (not slideshow) key-repeat

Outside slideshow, held ← / → uses the same **nav-hot + settle** idea:

- Every key: soft-install immediately (`update`, not sync `repaint` while hot).
- PreferCache / native climb and heavy chrome only after an **~80 ms** quiet settle.
- Without this, auto-repeat stacked `LoadReplace` + climb timers until the GUI froze.

`setSlideshowNavHot` is the shared flag (also suppresses new ZoomBlur builds during
a burst).

### Debug

Slideshow `qCDebug` traces (`[slideshow]…`, `[slideshow-paint]…`) are off by
default. Run with **`--debug`** to enable category `biltoo.slideshow`.


## Unitless phase (host clock)

Authority is continuous **position** (slide units), not absolute milliseconds:

- `position += wallΔt / intervalMs`
- phase in cycle = fractional part ∈ [0,1)
- fadeT derived from phase vs pureFrac = pureMs/interval

Changing interval only changes the **rate**. Position (and therefore the
visible slide + fade progress) is unchanged — no ms remap, no blip.


## Motion progress (unitless)

Ken Burns / pan-scan progress is **T ∈ [0,1]**. Wall clocks only supply Δt:

`T += Δt / pathDurationMs`

Interval changes alter the rate; T is not remapped from absolute milliseconds.
