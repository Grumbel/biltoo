# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2580.1-image-sticky-pan-zoom** (base `7d823d8`).

### Ownership transfer
- **ImageController::maybeCaptureStickyPanOnLeave** — Image leave sticky pan/zoom before underlay destroy
- **ImageController::setStickyZoomEnabled / releaseStickyZoom** — framing + view signals
- ImageView `setViewMode` sticky block is one call; sticky zoom APIs are thin routers

### Prior
**2579.1** HudChrome appearance mutators; ViewShellChrome centre progress  
**2578.1** GalleryController owns pixel-mix / loading tile counts  
**2577.1** HudModel pure status helpers; ViewShellChrome::restoreToolCursor

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
- setViewMode body remains mode shell (crop/attention leave + detach + enter)
- public thin routers beyond materials (keep until dual/callers migrate)
- slideshow coupling on setHudVisible (SlideshowController host)

### Apply
```bash
git pull --ff-only …/biltoo-2580.1-image-sticky-pan-zoom-7d823d8.bundle HEAD
```
