# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2670.1-workspace-crop-tiles-only` (base `2e49220`).

### This tip
Workspace crop flashed "Crop / No image" for a selected tile that had **no
soft/source sample** (tiles + optional LQIP only). `cropTargetItem` /
`enterCropModeFromUi` now accept a selected item with a non-empty path;
`prepareCropModeFullImage` still loads host/full. `transformTargets` filters
stale selection like `targetItem`.

### Apply
```bash
git pull --ff-only …/biltoo-2670.1-workspace-crop-tiles-only-2e49220.bundle HEAD
```
