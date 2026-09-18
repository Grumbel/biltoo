# Display pixels (tiles + LQIP)

**LQIP underlay + grid tiles.** SoftOnly whole-frame encode is removed from the
host soft-preview job.

| Mode | Policy |
|------|--------|
| Gallery / Workspace | LQIP + tiles; no SoftOnly job |
| Image mode | LQIP + tiles when durable or tileLodWanted |
| Slideshow | LQIP seed + quality job; SoftOnly encode not used |
| `startSoftPreviewJob` | LQIP from cache/Store only + size probe |

Filmstrip still uses `scheduleSoftPixels` for small thumbs (separate path).
