# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2668.1-crop-soft-uv-underlay` (base `2e49220`).

### Stack
… 2667.1 tile viewport map …
**2668.1** — After crop Apply: do not stretch full-frame soft/LQIP into crop box

### This tip
- Crop *draft* preview is correct (full layout + draft rect).
- After Apply, contentRect is crop-sized; host-raw soft/LQIP was still full
  page → paint stretched full → crop box = squish (PDF and large images).
- Underlay paint UV-crops sample via scaleCropRect when durable crop is set
  and sample aspect ≠ contentRect (already-baked soft skips).

### Apply
```bash
git pull --ff-only …/biltoo-2668.1-crop-soft-uv-underlay-2e49220.bundle HEAD
```
