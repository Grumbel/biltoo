# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2519.1-own-image-zoom-fit** (base `7d823d8`).

### Ownership transfer
- **zoomFit / zoomFill / zoomReset / zoomViewBy** on ImageController
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry / setWorkspaceDefaultViewScale (view-matrix shell)

### Next thinning candidates
- setWorkspaceDefaultViewScale
- setViewMode body (mode shell by design)
- appearance / paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2519.1-own-image-zoom-fit-7d823d8.bundle HEAD
```
