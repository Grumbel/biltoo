# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2582.1-pending-decode-mode-counts** (base `7d823d8`).

### Ownership transfer
- **GalleryController::uniqueBlankPathCount** — unique blank paths for pending decode
- **WorkspaceController::uniqueWeakPathCount** — unique ≤LQIP / blank paths
- **SlideshowController::pendingQualityWorkCount** — raster queue + soft current slide
- ImageView::pendingDecodeCount only sums load-gate + mode controller counts

### Prior
**2581.1** Viewport drag events on shell; transform undo with geometry  
**2580.1** ImageController sticky pan leave + sticky zoom  
**2579.1** HudChrome appearance mutators; ViewShellChrome centre progress

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
git pull --ff-only …/biltoo-2582.1-pending-decode-mode-counts-7d823d8.bundle HEAD
```
