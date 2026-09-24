# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2591.1-crop-appearance** (base `7d823d8`).

### Ownership transfer
- **CropController** owns crop appearance:
  - `storeCropAppearance`
  - `loadRestoreCropAppearance`
  - `restoreSessionCropAppearance`
  - `applyCropAppearance`
  - `emitCropApplyAppearance`
- New TU: `crop/cropcontroller_appearance.cpp`
- ImageView methods are thin routers (undo command / host API unchanged)

### Prior
**2590.1** ImageController copy appearance; SessionAppearance soft materialize  
**2589.1** SessionAppearance layout size; mode controllers own print  
**2588.1** ImageController owns resetContentAppearanceForTargets

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist host residual,
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
git pull --ff-only …/biltoo-2591.1-crop-appearance-7d823d8.bundle HEAD
```
