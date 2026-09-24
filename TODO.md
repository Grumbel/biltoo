# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2526.1-own-link-hover-select-all** (base `7d823d8`).

### Ownership transfer
- **TextLayerController::updateMouseMoveLinkHover** — Image-mode page-link hover tip + cursor
- **WorkspaceController::tryKeyPressSelectAll** — Gallery/Workspace Ctrl/Cmd+A
- ImageView input dispatch routes directly (no residual wrappers)

### Prior in this stack (2522–2525)
- Transform reset; attention session change; gallery open/focus; text link press
- zoomIn/Out; angleAt → PlacementLinear; colour grade on ImageController

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition / pan shell

### Next thinning candidates
- appearance apply/commit residual
- status text composition
- paint / pan / remaining input shell
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2526.1-own-link-hover-select-all-7d823d8.bundle HEAD
```
