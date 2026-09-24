# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2587.1-workspace-canvas-place** (base `7d823d8`).

### Ownership transfer
- **WorkspaceController** owns multi-item canvas add/place:
  - `addImageForSession`
  - `placeOrMoveImageAt`
- New TU: `workspace/workspace_canvas_place.cpp`
- `hostPathOrderAppendRow` for Explicit pathOrder growth from place/add
- ImageView public APIs are thin routers

### Prior
**2586.1** Workspace owns LoadAdd footprint + placement  
**2585.1** Pipeline owns applyProbedImageSize  
**2584.1** Pipeline owns blocking export display materialize

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
- resetContentAppearanceForTargets (appearance host residual)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2587.1-workspace-canvas-place-7d823d8.bundle HEAD
```
