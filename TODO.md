# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2512.1-own-workspace-clear-session** (base `7d823d8`).

### Ownership transfer
- **clearWorkspace / validateUniqueLiveSessionIds** on WorkspaceController
  (`workspace_clear.cpp`)
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- geometry undo command helpers
- setViewMode / mode enter-leave shell
- prepareImageModeCanvas / clearSceneKeepingStashes

### Apply
```bash
git pull --ff-only …/biltoo-2512.1-own-workspace-clear-session-7d823d8.bundle HEAD
```
