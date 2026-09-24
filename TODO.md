# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2502.1-own-workspace-reorder** (base `7d823d8`).

### Ownership transfer
- **reorderItemsByPaths** on WorkspaceController (`workspace_paths.cpp`)
- ImageView thin router (Gallery / pipeline / MainWindow still call ImageView)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` / `rebindWorkspaceSession`
- geometry undo command helpers
- selection / transformTargets / focus / destroyCanvasItem

### Apply
```bash
git pull --ff-only …/biltoo-2502.1-own-workspace-reorder-7d823d8.bundle HEAD
```
