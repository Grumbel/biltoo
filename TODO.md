# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2594.1-flush-applied-content** (base `7d823d8`).

### Ownership transfer
- **ImageController::flushAppliedContentToItemWorld** — mode-leave applied→sparse
  commit + clear live applied residual
- ImageView thin router (setViewMode still calls host flush)
- **persistSessionAppearanceSlot** uses SessionAppearance::persistPathContentAppearance
  for XDG write (no duplicated fill/save)

### Prior
**2593.1** SessionAppearance owns capture assemble + path persist  
**2592.1** Fix workspace place LoadAdd / private host access  
**2591.1** CropController owns crop appearance store/restore/apply

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / remember / persistSessionAppearanceSlot host residual,
setItemSessionId (identity host residual),
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- persistSessionAppearanceSlot (still host residual after XDG thin)
- setItemSessionId (identity scrub + color lag)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2594.1-flush-applied-content-7d823d8.bundle HEAD
```
