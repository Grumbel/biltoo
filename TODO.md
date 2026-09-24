# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2578.1-gallery-pixel-mix-counts** (base `7d823d8`).

### Ownership transfer
- **GalleryController::countLoadingTileStats** — blank / weak for HUD loading line
- **GalleryController::countDebugPixelMix** — blank/lqip/soft/higher/climbing
- ImageView status only gathers host context and calls HudModel + gallery counts

### Prior
**2577.1** HudModel pure status helpers; ViewShellChrome::restoreToolCursor  
**2576.1** Own canvas material mutators on ViewShellChrome  
**2575.1** Own paintEvent timing on HudChrome; drop empty group TU

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration (mode branch + climb labels)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers beyond materials (keep until dual/callers migrate)
- HUD appearance setters (slideshow sync residual on setHudVisible)

### Apply
```bash
git pull --ff-only …/biltoo-2578.1-gallery-pixel-mix-counts-7d823d8.bundle HEAD
```
