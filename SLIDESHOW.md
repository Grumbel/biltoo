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

## Pixels

For any path on screen, **each draw** chooses:

```
if better raster ready for path → blit it
else                            → blit soft placeholder
```

No events in the draw path. Decode is started ahead of time and stored as a
pollable buffer. When better pixels arrive, the **next** draw uses them.

**Camera is resolution-invariant.** Dest rect and Ken Burns path are functions
of **aspect ratio + motionT + biases + viewport** only — never of the raster’s
pixel width/height. Soft and sharp frames with the same aspect share one camera
path; only sampling sharpness changes. Do not upscale soft to “native size” to
fake matching geometry (that produced multi-megapixel phase buffers).

**Decode target** (long edge):

```
ladder(ceil(viewportLong × DPR × motionHeadroom))
```

capped at the image ladder max (2048). Headroom covers Ken Burns zoom past
1:1 cover. This is not “max / native.”

**Letterbox ZoomBlur** is a separate low-res underlay cache keyed by
path + viewport. Keep the previous underlay until the new key is ready; never
discard it on navigation just because the new blur has not finished.

**Pause exception:** while paused the clock (and usually redraws) stop. If
sharper pixels arrive for the visible path, schedule **one redraw**. That is
only “pixels ready → update view,” not transition start/cancel/hold logic.

Do not wire “decode finished” into start/cancel/hold of the pure wall clock.

## Scheduling

A single wall-clock phase decides the segment (dwell vs transition) and which pair (A, B). Rendering draws the buffers for that phase. There is no second scheduler.

The pure-phase path (`setSlideshowPhase`) is the only transition implementation
for Crossfade, Fade-to-black, Slide, and None (hard cut). The older live
dual-blit / snapshot overlay path is retired.

## Pause

While paused, the clock does not advance. The pause state stays visible until resume or stop.

### User navigation while paused

← / → (and filmstrip) must still show the new slide. The pure phase buffers are
updated immediately (`setSlideshowPhase` to the current path); the underlay may
load asynchronously. Unpausing must not be required to see the navigated image.

### Debug

Slideshow `qCDebug` traces (`[slideshow]…`, `[slideshow-paint]…`) are off by
default. Run with **`--debug`** to enable category `biltoo.slideshow`.
