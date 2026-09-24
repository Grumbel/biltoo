# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2540.1-own-climb-activity-label** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::imageModeClimbActivityLabel** — Image-mode climb
  status string (need/have, path-raster pending, ThumtooCache pending)
- ImageView statusTextImageMode calls the pipeline; drop private method

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance commit orchestration / paint / remaining status composition

### Next thinning candidates
- copySessionAppearance / remaining appearance_commit
- pixelQualityLabel residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2540.1-own-climb-activity-label-7d823d8.bundle HEAD
```
