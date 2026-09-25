# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2685.1-assemble-then-smooth` (base `932ed5c`).

### 2685 — real smooth-seam fix
Per-tile `drawImage` + `SmoothPixmapTransform` **clamps** at each cell → seams.
Expand/order hacks cannot fix that.

**Correct model:** blit exclusive tile patches **1:1** into one buffer, then
**one** filtered `drawImage` of that buffer (`paint_tile_patches` /
`paint_draw_plan`). Nearest path still draws tiles individually.

### Apply
```bash
git pull --ff-only …/biltoo-2685.1-assemble-then-smooth-932ed5c.bundle HEAD
```
