# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2559.1-own-view-matrix-shell** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::refreshScrollBarGeometry** — AsNeeded bar stale-range fix
- **ViewShellChrome::applyModeViewportPolicy** — Gallery BoundingRect vs
  Image/Workspace FullViewportUpdate
- `ImageView::refreshScrollBarGeometry` / `setActiveMode` viewport policy thin

### Prior
**2558.1** Own drawBackground on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (mode + layout bag; viewport policy on shell),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freezeItemAppearance host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2559.1-own-view-matrix-shell-7d823d8.bundle HEAD
```
