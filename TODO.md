# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2510.1-own-workspace-set-paths** (base `7d823d8`).

### Ownership transfer
- **setPaths** on WorkspaceController (`workspace_paths.cpp`)
- ImageView thin public router for MainWindow / session load

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- geometry undo command helpers (`pushItemGeometryCommand` + command class)
- validateUniqueLiveSessionIds
- mode enter/leave / clearWorkspace / clearLiveCanvas shell

### Apply
```bash
git pull --ff-only …/biltoo-2510.1-own-workspace-set-paths-7d823d8.bundle HEAD
```
