# Slideshow

## Timeline

One continuous wall clock. For each slide **A**, then **B**, then **C**, …:

```
[---- interval − transition : A only ----][--- transition : A + B ---]
[---- interval − transition : B only ----][--- transition : B + C ---]
[---- interval − transition : C only ----][--- transition : C + D ---]
 …
```

Definitions:

| Name | Meaning |
|------|---------|
| **interval** | Time from the start of one slide’s “only” period to the start of the next slide’s “only” period |
| **transition** | Length of the A+B (or B+C, …) overlap at the end of that interval |
| **dwell** | The “only” part: `interval − transition` |

So each slide is on screen for a full **interval** of wall time: first alone, then sharing the screen during the transition, then the next slide continues alone.

## What is on screen

### Dwell (A only)

- Draw **A** at full opacity.
- Dwell motion (Ken Burns / pan-zoom) runs on **A**.

### Transition (A + B)

- Still drawing **A** and **B**.
- **Opacity** is what the transition controls (crossfade, or fade-through-black, etc.).
- At transition **start**: A is dominant (opacity A ≈ 1, B ≈ 0).
- At transition **midpoint**: B becomes the dominant image.
- At transition **end**: B is the only image that matters (opacity B = 1, A = 0).
- Dwell motion continues across the whole **interval** (dwell + transition). It is not paused for the fade; the transition is opacity (and similar), not a substitute for motion.

After the transition, the next dwell is **B only**, with the same rules.

## Pixels

For any path on screen:

- Prefer **full-resolution** pixels when they are ready.
- If not, use a **preview** scaled to the image’s **native size** so layout and motion match full-res.
- When full-res arrives, replace the buffer in place — sharper, same geometry.

## Scheduling

A single wall-clock phase decides the segment (dwell vs transition) and which pair (A,B). Rendering only draws the buffers for that phase. There is no second scheduler and no need for start/cancel/hold chains to decide *when* slides change.

## Pause

While paused, the clock does not advance. The pause state stays visible until resume or stop.
