# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2679.1-coverage-overdraw-clamp` (base `932ed5c`).

### Implemented (from RESEARCH_TILE_OVERLAP)
1. **Last-tile content rect** — `tile_content_rect` extends last column/row to
   native AABB so scale>0 no longer leaves 1–N content px uncovered (odd widths
   after floor-half).
2. **Overdraw clamp** — `paint_seam_overdraw_content`: min(0.75/dpc, 1.0 content
   unit) on ImageItem + `paint_draw_plan`.
3. **Tests** — odd-width full coverage; overdraw clamp.

Research: [docs/RESEARCH_TILE_OVERLAP.md](docs/RESEARCH_TILE_OVERLAP.md) §13.

### Residual
- JPEG independent encode of the shared 257 strip (visual seam at coarse/upscale).
- Runtime QA of lower-zoom edges after this tip.

### Stack (base `932ed5c`)
- 2677 smooth only at 1×/2× density
- 2678 research doc
- 2679 coverage + overdraw clamp

### Apply
```bash
git pull --ff-only …/biltoo-2679.1-coverage-overdraw-clamp-932ed5c.bundle HEAD
```
