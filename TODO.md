# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2597.1-remember-propagate** (base `7d823d8`).

### Ownership transfer
- **ImageController::rememberItemState** — pose/path snapshot without color-lag promote
- **ImageController::propagateSessionAppearanceToViews** — filmstrip re-emit + mode post
- ImageView host methods are thin routers

### Prior
**2596.1** Pipeline owns high-res export paint + content bounds  
**2595.1** ImageController owns setItemSessionId + persistSessionAppearanceSlot  
**2594.1** ImageController owns flushAppliedContentToItemWorld

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freezeItemAppearance host residual (SessionAppearance durable path),
renderExportImage (canvas bg + pipeline paint orchestration),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- takePendingSessionBindForNewItem
- freezeItemAppearance (already mostly SessionAppearance)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2597.1-remember-propagate-7d823d8.bundle HEAD
```
