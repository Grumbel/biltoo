# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2575.1-own-paint-timing-drop-group-tu** (base `7d823d8`).

### Ownership transfer
- **HudChrome::runTimedPaint** — BILTOO_PERF / THUMTOO_DEBUG paint duration
- ImageView `paintEvent` is a thin shell (lambda → QGraphicsView::paintEvent)
- Dropped empty `imageview_group.cpp` TU (WorkspaceController + ViewShellChrome own group input)

### Prior
**2574.1** Own wheel and resize dispatch on ViewShellChrome  
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

### Apply
```bash
git pull --ff-only …/biltoo-2575.1-own-paint-timing-drop-group-tu-7d823d8.bundle HEAD
```
