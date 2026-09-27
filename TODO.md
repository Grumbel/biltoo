# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.15-paint-drew-tiles` (base `bcbb97e`).

### Verified (2714.14) + paint truth (2714.15)
`TileLodController::paint` / `paint_draw_plan` now return **true only if tile
pixels were actually drawn**. Previously `plan.any_tile` could be true while
every resolve failed — virtual cover then skipped EMB and showed blank/wrong.

### Apply
```bash
git pull --ff-only …/biltoo-2714.15-paint-drew-tiles-bcbb97e.bundle HEAD
```
