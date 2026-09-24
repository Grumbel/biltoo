# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2626.1-host-narrow-fititem** (base `7d823d8`).

### This tip — DisplayPipelineHost narrowing (cluster: fit)
- Removed `fitItem` and `currentFitAspectMode` pure virtuals
- Pipeline / crop / slideshow / image controller use `hostImage().fitItem` and
  `hostImage().framing().aspectMode()` (or `fitItem` / `framing()` on self)

### Host virtuals remaining: ~57 (was ~59 after 2625)

### Apply
```bash
git pull --ff-only …/biltoo-2626.1-host-narrow-fititem-7d823d8.bundle HEAD
```
