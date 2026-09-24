# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2498.1-own-workspace-scene** (base `7d823d8`).

### Ownership transfer
- **updateSceneRect / findEmptyPlacement / hasContent** on WorkspaceController
- ImageView thin routers for DisplayPipelineHost + MainWindow

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- flip / rotate (cross-mode)
- `duplicateSelected` (Workspace + Gallery)
- setWorkspacePaths / rebind / destroy canvas orchestration

### Apply
```bash
git pull --ff-only …/biltoo-2498.1-own-workspace-scene-7d823d8.bundle HEAD
```
