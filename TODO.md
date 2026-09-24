# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2570.1-own-mode-leave-hud-index-pure** (base `7d823d8`).

### Ownership transfer
- **ViewModeFlags::shouldCaptureStickyPanOnLeave** — Image leave sticky pan
- **HudModel::fileNameWithModifiedSuffix** — HUD filename ± modified mark
- **SessionAppearance::preferSessionListIndex** — document order when bound
- Host keeps setViewMode / hudFileName / sessionListIndex orchestration

### Prior
**2569.1** Own crop restore source and Gallery enter layout.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2570.1-own-mode-leave-hud-index-pure-7d823d8.bundle HEAD
```
