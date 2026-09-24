# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2503.1-own-workspace-rebind** (base `7d823d8`).

### Ownership transfer
- **rebindSession** on WorkspaceController (`workspace_paths.cpp`)
- ImageView thin router; `hostValidateUniqueLiveSessionIds` for identity checks

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- selection / transformTargets / focus / destroyCanvasItem

### Apply
```bash
git pull --ff-only …/biltoo-2503.1-own-workspace-rebind-7d823d8.bundle HEAD
```
