# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2598.1-bind-take-stub** (base `7d823d8`).

### Ownership transfer + fix
- **WorkspaceController::takePendingSessionBindForNewItem** / **purgeSatisfiedPendingBinds**
- ImageView session-bind routers are thin
- **tests/thumtoocache_appearance_stub.cpp** — link stub for unit tests that compile
  sessionappearance.cpp without the full thumtoo host (fixes packorderoverlay
  undefined refs for load/save/clear content appearance)

### Prior
**2597.1** ImageController owns rememberItemState + appearance propagate  
**2596.1** Pipeline owns high-res export paint + content bounds  
**2595.1** ImageController owns setItemSessionId + persistSessionAppearanceSlot

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
- freezeItemAppearance (already mostly SessionAppearance)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2598.1-bind-take-stub-7d823d8.bundle HEAD
```
