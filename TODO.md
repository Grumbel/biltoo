# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2534.1-own-hud-status-pure** (base `7d823d8`).

### Ownership transfer
- **HudModel::loadingLineWithGalleryExtras** — pure loading line + Gallery blank/weak
- **HudModel::placementFlipRotationSuffix** — pure rot/flip status suffix
- **SlideshowController::syncProgressTimerWithHud** — progress timer ↔ pinned HUD
- ImageView status/HUD setters stay thin hosts over HudChrome + pure formatters

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / remaining status orchestration

### Next thinning candidates
- appearance apply/commit residual
- remaining status composition (quality climb labels still view-coupled)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2534.1-own-hud-status-pure-7d823d8.bundle HEAD
```
