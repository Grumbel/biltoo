# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2517.1-own-geometry-undo-tu** (base `7d823d8`).

### Ownership transfer
- Geometry/content undo command bodies → `item/geometryundocommand.cpp`
- `imageview_transform_actions.cpp` is thin transform routers only (~90 lines)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration,
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin) / applyModeFlagsToLiveItems,
pushItemGeometryCommand / pushItemContentCommand (host API; body in item/)

### Next thinning candidates
- setViewMode body (mode shell — keep orchestration on ImageView by design)
- fitItem / appearance / paint / input event TUs (larger residual domains)

### Apply
```bash
git pull --ff-only …/biltoo-2517.1-own-geometry-undo-tu-7d823d8.bundle HEAD
```
