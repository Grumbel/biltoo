# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2572.1-own-input-dispatch** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::handleMousePress/Move/Release** — full try* dispatch chains
- **ViewShellChrome::handleKeyPress** / **handleMouseDoubleClick** / **handleLeave**
- ImageView QGraphicsView overrides are thin shell + base-class fallthrough

### Prior
**2571.1** Own live color bind and usable native pure policy.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / input one-line QGraphicsView overrides,
thin private try* routers (still used by shell via host controllers)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)
- drop ImageView thin private try* if no remaining callers

### Apply
```bash
git pull --ff-only …/biltoo-2572.1-own-input-dispatch-7d823d8.bundle HEAD
```
