# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2562.1-own-live-display-overlays** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::applyLiveDisplayOverlays** — placement flips + live grade
  when no applied ContentXform (filmstrip / soft-sample path)
- `ImageView::sessionAppearanceImage` keeps display-image host fetch

### Prior
**2561.1** Own rememberKind pure policy on SessionAppearance.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / sessionAppearanceImage host residual,
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2562.1-own-live-display-overlays-7d823d8.bundle HEAD
```
