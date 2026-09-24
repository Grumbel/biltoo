# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2573.1-drop-input-try-forwards** (base `7d823d8`).

### Ownership transfer
- Drop ImageView private thin try* input forwards (press/move/release/key
  phases) — ViewShellChrome dispatches controllers directly
- Keep `updateMouseInfo`, `restoreToolCursor`, `pushItemTransformUndo` host APIs

### Prior
**2572.1** Own input event dispatch on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground / input one-line QGraphicsView overrides

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2573.1-drop-input-try-forwards-7d823d8.bundle HEAD
```
