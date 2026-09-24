# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2588.1-reset-content-appearance** (base `7d823d8`).

### Ownership transfer
- **ImageController::resetContentAppearanceForTargets** — clear XDG/sparse content,
  identity live meta, restore native layout size, reinstall mode pixels, filmstrip emit
- ImageView public API is a thin router

### Prior
**2587.1** Workspace owns addImageForSession + placeOrMoveImageAt  
**2586.1** Workspace owns LoadAdd footprint + placement  
**2585.1** Pipeline owns applyProbedImageSize

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
contentLayoutSize host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- contentLayoutSize (SessionAppearance + size book host gather)
- persistSessionAppearanceSlot / captureState (appearance host residual)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2588.1-reset-content-appearance-7d823d8.bundle HEAD
```
