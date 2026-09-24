# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2541.1-own-pixel-quality-label** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::pixelQualityLabel** — on-screen quality tier + Gallery need/have
- **HudModel::imageModeStatusHeader** — pure "W×H · Zoom Z%" header
- ImageView keeps thin pixelQualityLabel forward; statusTextImageMode uses HudModel header

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance apply/resolve / paint / remaining status composition

### Next thinning candidates
- copySessionAppearance / applyStoredAppearance residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2541.1-own-pixel-quality-label-7d823d8.bundle HEAD
```
