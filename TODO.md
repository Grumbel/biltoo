# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2581.1-viewport-event-transform-undo** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::handleViewportEvent** — DragEnter/Move/Drop from QOpenGLWidget viewport
- **pushItemTransformUndo / hostPushItemTransformUndo** live next to geometry undo
  (`item/geometryundocommand.cpp`); ImageView input TU is event routing only
- Slimmed `imageview_input_events.cpp` (~161 → ~90 lines)

### Prior
**2580.1** ImageController sticky pan leave + sticky zoom  
**2579.1** HudChrome appearance mutators; ViewShellChrome centre progress  
**2578.1** GalleryController owns pixel-mix / loading tile counts

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
git pull --ff-only …/biltoo-2581.1-viewport-event-transform-undo-7d823d8.bundle HEAD
```
