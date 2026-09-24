# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2590.1-copy-appearance-soft** (base `7d823d8`).

### Ownership transfer
- **ImageController::copySessionAppearance** — store/live-donor content copy
- **SessionAppearance::softImageWithAppearanceSources** — pure soft materialize
- ImageView::imageWithSessionAppearance gathers ItemWorld state only
- ImageView::copySessionAppearance is a thin router

### Prior
**2589.1** SessionAppearance layout size; mode controllers own print  
**2588.1** ImageController owns resetContentAppearanceForTargets  
**2587.1** Workspace owns addImageForSession + placeOrMoveImageAt

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
git pull --ff-only …/biltoo-2590.1-copy-appearance-soft-7d823d8.bundle HEAD
```
