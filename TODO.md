# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2515.1-own-pipeline-invalidate-session** (base `7d823d8`).

### Ownership transfer
- **invalidateSessionLoads** on DisplayPipelineController
  (`displaypipelinecontroller_load.cpp`)
- ImageView thin router

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration,
setViewMode / setActiveMode mode shell

### Still on ImageView
- geometry undo command helpers
- applyItemModeFlags / applyModeFlagsToLiveItems

### Apply
```bash
git pull --ff-only …/biltoo-2515.1-own-pipeline-invalidate-session-7d823d8.bundle HEAD
```
