# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2833.4-tile-stretch-full-bitmap (on 2833.3 stack).

### 2833.4
- DrawPlan ExactTile: `src_uv` = full bitmap; stretch into content-space cell
  (DCT/floor-half size drift is not a failure)

### 2833.3
- TileLoadCoordinator: settled Failed stops zero-tile re-issue

### Required thumtoo
thumtoo-009.2-tile-size-stretch (on 008.1; includes 009.1 jpeg rgb path)

### Note
`TILE s=N ERROR` means host Failed the cell. With 009.2 + stretch, store hits
with drifted w/h succeed; paint maps full bitmap → grid dest.
