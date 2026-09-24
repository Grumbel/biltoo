# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2574.1-own-wheel-resize-dispatch** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::handleWheel** — Gallery zoom/scroll then Image zoom-about-cursor
- **ViewShellChrome::handleResize** — mode-specific post-resize (pack / quality / framing)
- ImageView `wheelEvent` / `resizeEvent` are thin shell + base-class

### Prior
**2573.1** Drop ImageView thin input try* forwards.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)
- paintEvent HUD timing → HudChrome

### Apply
```bash
git pull --ff-only …/biltoo-2574.1-own-wheel-resize-dispatch-7d823d8.bundle HEAD
```
