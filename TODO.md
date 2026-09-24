# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2558.1-own-draw-background** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::paintCanvasBackground** — checker / tile / content-blur materials
- **ViewShellChrome::paintBackground** — full drawBackground (canvas + gallery
  virtual placeholders + page-guide paper)
- **WorkspaceController::paintPageGuidePaper** — white sheet under images
- `ImageView::drawBackground` one-line shell dispatch
- `ImageView::paintCanvasBackground` thin export forward to shell

### Prior
**2557.1** Own drawForeground on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides

### Next thinning candidates
- setViewMode body (mode shell by design)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2558.1-own-draw-background-7d823d8.bundle HEAD
```
