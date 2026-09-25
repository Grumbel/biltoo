# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2675.1-overlap-exact-only-expand` (base `2e49220`).
**thumtoo:** `thumtoo-340.1-tile-overlap-5e47314`.

### Re-verify (2675)
- Dest expand **ExactTile only** (CoarserTile was wrongly expanding from parent 257 size).
- `paint_draw_plan` + `imageitem_interaction` both gated.
- `parent_uv_for_child` maps exclusive 256 of parent.
- thumtoo all cut sites use `tile_cell_pixel_rect` (no remaining `min(kTileSize)` cuts).

### Apply
```bash
git pull --ff-only …/biltoo-2675.1-overlap-exact-only-expand-2e49220.bundle HEAD
```
