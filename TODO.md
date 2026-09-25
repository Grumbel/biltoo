# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2688.1-overlap-zero` (base `932ed5c`).

### 2688 — biltoo kTileOverlap = 0
Match thumtoo. Parent UV still strips one column/row when bitmap > 256
(legacy Store). Removed dead `paint_seam_overdraw_content`.

### Content size grow/shrink during generation
Usually **soft/LQIP ladder** (different long edges installing on the item), not
tile overlap. Intrinsic size should stay native; if a path still resizes the
item from sample long-edge, that is separate from 257.

### Apply
```bash
git pull --ff-only …/biltoo-2688.1-overlap-zero-932ed5c.bundle HEAD
```
