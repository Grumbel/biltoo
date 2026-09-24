# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2560.1-own-applied-flush-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::mergeAppliedIntoDurable** — mode-leave applied→sparse merge
  (no color promote; preserve durable crop when applied has none)
- **SessionAppearance::resolveEditSessionId** — pure item / Image-mode current id
- `flushAppliedContentToItemWorld` / `resolveContentEditSessionId` keep host
  ItemWorld + live-tile orchestration

### Prior
**2559.1** Own view-matrix shell helpers on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freezeItemAppearance / flushApplied host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2560.1-own-applied-flush-pure-7d823d8.bundle HEAD
```
