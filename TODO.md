# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2520.1-own-workspace-place-remove-ids** (base `7d823d8`).

### Ownership transfer
- **setWorkspaceDefaultViewScale** on ImageController
- **placeSessionIdsOnCanvas / removeCanvasSessionIds** on WorkspaceController
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell)

### Next thinning candidates
- removeWorkspaceSessionId (session remove orchestration)
- setViewMode body (mode shell by design)
- appearance / paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2520.1-own-workspace-place-remove-ids-7d823d8.bundle HEAD
```
