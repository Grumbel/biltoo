# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.10-no-focusfull-overview (base 2085c07).

### 2815.10 — true settle CPU root cause
- scheduleTileSynthOrPyramid no longer calls scheduleTilePyramid on cold paths
- Filmstrip cold path: scheduleProbe only (was FocusFull per archive member)
- Neighbor prefetch: probe only, not FocusFull
- FocusFull fully decodes each archive JPEG at scale 0; overview was scheduling
  that for every MetArt-style member → 100% CPU after UI looked settled

### Still useful
- thumtoo-focus-full-no-busy-wait (worker wait, no archive pyramid coalesce)
- Rebuild thumtoo + biltoo together

### Apply
```bash
git pull --ff-only …/biltoo-2815.10-no-focusfull-overview-2085c07.bundle HEAD
```
