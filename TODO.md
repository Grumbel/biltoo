# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2505.1-own-workspace-path-select** (base `7d823d8`).

### Ownership transfer
- **pathOnLiveCanvas / pathOccurrenceCount / selectAllCanvasItems** on WorkspaceController
- ImageView thin routers (prefetch host still exposes pathOnLiveCanvas)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- selection queries / transformTargets / primaryItem / targetItem / destroyCanvasItem

### Apply
```bash
git pull --ff-only …/biltoo-2505.1-own-workspace-path-select-7d823d8.bundle HEAD
```
