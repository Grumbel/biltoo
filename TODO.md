# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2593.1-capture-persist-policy** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::persistPathContentAppearance** — path XDG write/clear
- **SessionAppearance::assembleCaptureState** — interaction snapshot assembly
- ImageView::persistDurableContentAppearance / captureState gather host inputs only

### Prior
**2592.1** Fix workspace place LoadAdd / private host access  
**2591.1** CropController owns crop appearance store/restore/apply  
**2590.1** ImageController copy appearance; SessionAppearance soft materialize

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persistSessionAppearanceSlot host residual,
setItemSessionId (identity host residual),
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- persistSessionAppearanceSlot (still substantial host residual)
- setItemSessionId (identity scrub + color lag)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2593.1-capture-persist-policy-7d823d8.bundle HEAD
```
