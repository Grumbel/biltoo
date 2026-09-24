# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2511.1-own-workspace-clear-live** (base `7d823d8`).

### Ownership transfer
- **clearInteractionState / clearLiveCanvas** on WorkspaceController (`workspace_clear.cpp`)
- ImageView thin routers; clearWorkspace still orchestrates on ImageView

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- geometry undo command helpers
- validateUniqueLiveSessionIds
- clearWorkspace / setViewMode / mode enter-leave shell

### Apply
```bash
git pull --ff-only …/biltoo-2511.1-own-workspace-clear-live-7d823d8.bundle HEAD
```
