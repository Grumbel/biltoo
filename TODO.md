# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2494.1-own-workspace-tool-drag-mode** (base `7d823d8`).

### Ownership transfer
- **applyToolDragMode** on WorkspaceController (with tool state)
- ImageView thin router; Workspace enter calls applyToolDragMode locally

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Apply
```bash
git pull --ff-only …/biltoo-2494.1-own-workspace-tool-drag-mode-7d823d8.bundle HEAD
```
