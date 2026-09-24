# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2576.1-own-canvas-material-mutators** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome** owns canvas material mutators:
  `setBackgroundColor` / `Alt` / `Pattern` / `CheckerboardWorkspaceOnly`,
  `setWorkspaceBackground` / `ShowDefault`, `setViewBackground`
- ImageView public setters are thin routers (MainWindow / Preferences API)
- `drawBackground` / `drawForeground` / `paintCanvasBackground` remain one-line shells

### Prior
**2575.1** Own paintEvent timing on HudChrome; drop empty group TU  
**2574.1** Own wheel and resize dispatch on ViewShellChrome  
**2573.1** Drop ImageView thin input try* forwards

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin background routers (MainWindow API surface)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers beyond materials (MainWindow API — keep until dual/callers migrate)
- statusText composition further into HudModel (host-data still required)

### Apply
```bash
git pull --ff-only …/biltoo-2576.1-own-canvas-material-mutators-7d823d8.bundle HEAD
```
