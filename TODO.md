# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2507.1-own-workspace-primary-target** (base `7d823d8`).

### Ownership transfer
- **primaryItem / targetItem** on WorkspaceController (`workspace_selection.cpp`)
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- destroyCanvasItem
- validateUniqueLiveSessionIds

### Apply
```bash
git pull --ff-only …/biltoo-2507.1-own-workspace-primary-target-7d823d8.bundle HEAD
```
