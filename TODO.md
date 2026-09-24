# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2557.1-own-draw-foreground** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::paintForeground** — full drawForeground body: scene chrome
  (page guide, text overlays, gallery frames), bare-Gallery early-out, DPR
  identity transform, paintViewportOverlays
- `ImageView::drawForeground` is a one-line shell dispatch

### Prior
**2556.1** Own scene foreground chrome on Text and Workspace controllers.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
drawForeground one-line dispatch (QGraphicsView override)

### Next thinning candidates
- setViewMode body (mode shell by design)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)
- drawBackground / canvas background residual

### Apply
```bash
git pull --ff-only …/biltoo-2557.1-own-draw-foreground-7d823d8.bundle HEAD
```
