# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2567.1-own-native-path-prefer-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::pickNativeSize** — logical → book → optional store cache
- **SelectionGeometry::preferUniquePathItem** — unique selected / unique live
  path preference for duplicates
- `contentLayoutSize` / `findPreferredItemForPath` keep host resolution loops

### Prior
**2566.1** Own layout size pure helpers on SessionAppearance.

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
git pull --ff-only …/biltoo-2567.1-own-native-path-prefer-pure-7d823d8.bundle HEAD
```
