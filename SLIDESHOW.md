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
if full-res ready for path → blit full-res
else                       → blit low-res placeholder (scaled to native size)
```

No events in the draw path. Full decode is started ahead of time and stored as
something pollable (a future / ready buffer). When it becomes ready, the
**next** draw simply sees full-res and uses it. Same geometry either way, so
the switch is seamless (sharper, no layout jump).

**Pause exception:** while paused the clock (and usually redraws) stop. If full
pixels arrive for the visible path, schedule **one redraw** so the sharper
image appears. That is only “pixels ready → update view,” not transition
start/cancel/hold logic.

Do not wire “decode finished” into start/cancel/hold of the transition.

## Scheduling

A single wall-clock phase decides the segment (dwell vs transition) and which pair (A, B). Rendering draws the buffers for that phase. There is no second scheduler.

## Pause

While paused, the clock does not advance. The pause state stays visible until resume or stop.
