# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2564.1-own-status-suffix-formatters** (base `7d823d8`).

### Ownership transfer
- **HudModel::qualityStatusSuffix** — quality ± optional edge px
- **HudModel::nativeSizeStatusSuffix** — real native W×H (skip placeholders)
- **HudModel::pendingLoadStatusSuffix** / **labeledStatusSuffix** / **editedStatusSuffix**
- `statusTextMultiItem` / `statusTextImageMode` keep host snapshot assembly

### Prior
**2563.1** Own persist color/crop policy on SessionAppearance.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2564.1-own-status-suffix-formatters-7d823d8.bundle HEAD
```
