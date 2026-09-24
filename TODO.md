# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2561.1-own-remember-kind-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::RememberKind** / **rememberKind** — pure policy for
  rememberItemState (Skip / WritePlacementOnly / WritePathFreeze)
- `ImageView::rememberItemState` keeps freeze + ItemWorld writes

### Prior
**2560.1** Own applied flush pure merge on SessionAppearance.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2561.1-own-remember-kind-pure-7d823d8.bundle HEAD
```
