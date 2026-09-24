# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2501.1-own-workspace-duplicate** (base `7d823d8`).

### Ownership transfer
- **duplicateSelected** on WorkspaceController (`workspace_duplicate.cpp`)
- ImageView thin router; `hostSessionAppearanceImage` for appearance samples

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` / rebind orchestration
- geometry undo command helpers
- selection / transformTargets / focus

### Apply
```bash
git pull --ff-only …/biltoo-2501.1-own-workspace-duplicate-7d823d8.bundle HEAD
```
