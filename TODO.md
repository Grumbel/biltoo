# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2586.1-workspace-load-place** (base `7d823d8`).

### Ownership transfer
- **WorkspaceController** owns LoadAdd / drop placement:
  - `installFullPreservingWorkspaceFootprint`
  - `applyPendingBindScenePos`
  - `placeNewLoadAddItem`
- New TU: `workspace/workspace_load_place.cpp`
- ImageView session-bind host methods are thin routers
- `takePendingSessionBindForNewItem` / `purgeSatisfiedPendingBinds` stay (session book)

### Prior
**2585.1** Pipeline owns applyProbedImageSize; modes includes slimmed  
**2584.1** Pipeline owns blocking export display materialize  
**2583.1** Fix hostCentreProgress dup; pad colour + edit marks on chrome

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
contentLayoutSize / placeOrMoveImageAt / addImageForSession host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- placeOrMoveImageAt / addImageForSession (still large host residual)
- contentLayoutSize (SessionAppearance + size book host gather)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2586.1-workspace-load-place-7d823d8.bundle HEAD
```
