# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2577.1-status-pure-restore-cursor** (base `7d823d8`).

### Ownership transfer
- **HudModel** pure status helpers: `shouldAppendQualityEdgePx`,
  `isThumtooDebugEnabled`, `thumtooDebugStatusSuffix`
- ImageView status composition is thinner (host data + HudModel)
- **ViewShellChrome::restoreToolCursor** — tool cursor after pan / chrome drag
- ImageView `restoreToolCursor` is a thin shell router

### Prior
**2576.1** Own canvas material mutators on ViewShellChrome  
**2575.1** Own paintEvent timing on HudChrome; drop empty group TU  
**2574.1** Own wheel and resize dispatch on ViewShellChrome

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host-data gather (gallery mix counts, climb labels)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers beyond materials (keep until dual/callers migrate)
- gallery pixel-mix counting helper (still needs live item walk)

### Apply
```bash
git pull --ff-only …/biltoo-2577.1-status-pure-restore-cursor-7d823d8.bundle HEAD
```
