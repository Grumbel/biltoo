# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2521.1-own-workspace-session-remove** (base `7d823d8`).

### Build fix
- ViewModeFlags uses `int` mode (ImageView::ViewMode is nested in ImageView)

### Ownership transfer
- **removeWorkspaceSessionId** + session-id remove helpers on WorkspaceController
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell)

### Next thinning candidates
- setViewMode body (mode shell by design)
- appearance / paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2521.1-own-workspace-session-remove-7d823d8.bundle HEAD
```
