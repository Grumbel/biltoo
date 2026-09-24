# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2500.1-own-workspace-path-membership** (base `7d823d8`).

### Ownership transfer
- **collectDoomedItems / destroyDoomedItems / defaultStateForPath** on WorkspaceController
- ImageView private thin wrappers (setWorkspacePaths / session bind)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `duplicateSelected` (Workspace + Gallery)
- `setWorkspacePaths` / `finishSetWorkspacePaths` / rebind orchestration
- geometry undo command helpers

### Apply
```bash
git pull --ff-only …/biltoo-2500.1-own-workspace-path-membership-7d823d8.bundle HEAD
```
