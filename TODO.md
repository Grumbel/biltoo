# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2563.1-own-persist-color-crop-policy** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::preferDurableColor** — freeze lag must not overwrite
  ItemWorld durable Color (grade-commit authority)
- **SessionAppearance::shouldWriteCropToPathStore** — path XDG crop only for
  unbound tiles
- `persistSessionAppearanceSlot` / `persistDurableContentAppearance` keep host
  ItemWorld + ThumtooCache writes

### Prior
**2562.1** Own live display overlays on SessionAppearance.

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
git pull --ff-only …/biltoo-2563.1-own-persist-color-crop-policy-7d823d8.bundle HEAD
```
