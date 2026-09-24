# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2589.1-layout-size-print** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::resolveContentLayoutSize** — pure layout size policy
  (ImageView gathers host sizes/state only)
- **WorkspaceController::renderForPrint** — page guide / content high-res print
- **ImageController::renderForPrint** — Image-mode high-res print
- ImageView::renderForPrint routes by mode; scene fallback stays on shell
- paintHighResExportItems public on host ops (print/export)

### Prior
**2588.1** ImageController owns resetContentAppearanceForTargets  
**2587.1** Workspace owns addImageForSession + placeOrMoveImageAt  
**2586.1** Workspace owns LoadAdd footprint + placement

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- persistSessionAppearanceSlot / captureState (appearance host residual)
- setItemSessionId (identity host residual)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2589.1-layout-size-print-7d823d8.bundle HEAD
```
