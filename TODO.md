# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2568.1-own-content-edit-detect-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::shouldAugmentFromPathStore** — unbound path XDG layout load
- **SessionAppearance::itemShowsContentEdit** — per-item content-edit presence
  (sparse / durable / live applied / path XDG)
- `contentLayoutSize` / `targetHasContentAppearance` keep host queries

### Prior
**2567.1** Own pickNativeSize and path prefer pure helpers.

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
git pull --ff-only …/biltoo-2568.1-own-content-edit-detect-pure-7d823d8.bundle HEAD
```
