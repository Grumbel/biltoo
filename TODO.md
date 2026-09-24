# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2583.1-fix-hostcentre-progress-dup** (base `7d823d8`).

### Ownership transfer
- **Fix:** duplicate `hostCentreProgress()` in imageview_host_accessors.inc (compile error)
- **SlideshowController::padColorForPaint** — letterbox pad colour resolution
- **ViewShellChrome::setContentEditMarksVisible** — live-tile + scene/viewport refresh
- ImageView `slideshowPadColor` / `setContentEditMarksVisible` are thin routers

### Prior
**2582.1** Mode controllers own pending-decode path counts  
**2581.1** Viewport drag events on shell; transform undo with geometry  
**2580.1** ImageController sticky pan leave + sticky zoom

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- public thin routers beyond materials (keep until dual/callers migrate)
- slideshow coupling on setHudVisible (SlideshowController host)

### Apply
```bash
git pull --ff-only …/biltoo-2583.1-fix-hostcentre-progress-dup-7d823d8.bundle HEAD
```
