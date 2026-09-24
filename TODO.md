# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2566.1-own-layout-size-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::layoutSizeOrNative** — content layout size with native fallback
- **SessionAppearance::galleryCellAspectStale** — Gallery pack clip aspect check
- `contentLayoutSize` / `applyProbedImageSize` keep host size-book resolution
- loading HUD weak-tile threshold uses **DisplayQuality::kLqipMaxEdge**

### Prior
**2565.1** Own soft paint state pure on SessionAppearance.

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
git pull --ff-only …/biltoo-2566.1-own-layout-size-pure-7d823d8.bundle HEAD
```
