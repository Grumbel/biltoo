# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2495.1-own-workspace-stack-opacity** (base `7d823d8`).

### Ownership transfer
- **raise/lower** (item + selected) and **opacity** up/down/reset on WorkspaceController
- ImageView thin routers; helpers in `workspace_stack.cpp`

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView (multi-mode, not moved)
resetItemScale / resetItemRotation / resetItemShear (Workspace selection + Image target)
flip / rotate (pipeline bake + Image framing + Gallery pack)

### Apply
```bash
git pull --ff-only …/biltoo-2495.1-own-workspace-stack-opacity-7d823d8.bundle HEAD
```
