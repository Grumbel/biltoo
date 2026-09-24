# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2506.1-own-workspace-selection** (base `7d823d8`).

### Ownership transfer
- **transformTargets / selectBy* / selectedSession* / hasTransformTargets /
  hasSingleCropTarget** on WorkspaceController (`workspace_selection.cpp`)
- ImageView thin routers; validateUniqueLiveSessionIds remains on ImageView

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- primaryItem / targetItem / destroyCanvasItem
- validateUniqueLiveSessionIds

### Apply
```bash
git pull --ff-only …/biltoo-2506.1-own-workspace-selection-7d823d8.bundle HEAD
```
