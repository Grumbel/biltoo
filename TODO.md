# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2542.1-own-stored-appearance-apply** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::resolveStoredAppearance** — durable/path resolve + seed
- **DisplayPipelineController::applyStoredAppearance** — full-raster install or rematerialize
- ImageView keeps thin host/pipeline forwards (DisplayPipelineHost override surface)

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freeze/capture state / paint / remaining status composition

### Next thinning candidates
- copySessionAppearance residual
- freezeItemAppearance / captureState residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2542.1-own-stored-appearance-apply-7d823d8.bundle HEAD
```
