# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2533.1-own-drag-drop** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome** owns dragEnter / dragMove / drop (mime accept + filesDropped emit)
- ImageView overrides remain thin QGraphicsView routers; viewportEvent still forwards
  OpenGL viewport drops to the view

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2533.1-own-drag-drop-7d823d8.bundle HEAD
```
