# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2674.1-tile-overlap-paint-all-paths` (base `2e49220`).
**thumtoo:** `thumtoo-340.1-tile-overlap-5e47314` (257 encode).

### Overlap paint coverage
- `paint_draw_plan` (cover / slideshow / TileLodController::paint): expand dest + full UV for ExactTile
- `imageitem_interaction` Image/Workspace/Gallery path: same expand
- `parent_uv_for_child`: maps into exclusive portion of parent (not the +1 strip)
- Grid step still 256; ES2 may crop 257→256 and accept seams

### Apply
```bash
git pull --ff-only …/biltoo-2674.1-tile-overlap-paint-all-paths-2e49220.bundle HEAD
```
