# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2508.1-own-workspace-destroy** (base `7d823d8`).

### Ownership transfer
- **destroyCanvasItem** on WorkspaceController (`workspace_destroy.cpp`)
- ImageView thin host router; hostRememberItemState / hostPreserveUndoOnDestroy

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- validateUniqueLiveSessionIds

### Apply
```bash
git pull --ff-only …/biltoo-2508.1-own-workspace-destroy-7d823d8.bundle HEAD
```
