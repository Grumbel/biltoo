# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2509.1-own-workspace-finish-paths** (base `7d823d8`).

### Ownership transfer
- **finishPathsSet** on WorkspaceController (`workspace_paths.cpp`)
- ImageView thin private router for setWorkspacePaths

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` (membership ensure + load schedule orchestration)
- geometry undo command helpers
- validateUniqueLiveSessionIds

### Apply
```bash
git pull --ff-only …/biltoo-2509.1-own-workspace-finish-paths-7d823d8.bundle HEAD
```
