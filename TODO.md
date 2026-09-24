# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2595.1-session-id-persist-slot** (base `7d823d8`).

### Ownership transfer
- **ImageController::setItemSessionId** — path-conflict scrub across live/stash,
  list-index cache, live color lag seed
- **ImageController::persistSessionAppearanceSlot** — freeze → ItemWorld + path XDG
  + filmstrip signals
- ImageView host methods are thin routers

### Prior
**2594.1** ImageController owns flushAppliedContentToItemWorld  
**2593.1** SessionAppearance owns capture assemble + path persist  
**2592.1** Fix workspace place LoadAdd / private host access

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / remember host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- paintHighResExportItems / renderExportImage (export scene walk)
- freezeItemAppearance / rememberItemState (appearance host residual)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2595.1-session-id-persist-slot-7d823d8.bundle HEAD
```
