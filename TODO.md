# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2671.1-crop-keep-tiles` (base `2e49220`).

### This tip
Crop enter no longer suppresses tile LOD / skips draft-locked items in
TileLoadCoordinator. Sample freeze still blocks soft/ladder install thrash;
display uses the same tile paint path as Image/Workspace (soft ≤512 underlay
only until tiles cover).

### Apply
```bash
git pull --ff-only …/biltoo-2671.1-crop-keep-tiles-2e49220.bundle HEAD
```
