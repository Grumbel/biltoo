# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2496.1-own-workspace-placement-resets** (base `7d823d8`).

### Ownership transfer
- **resetItemScale / Rotation / Shear** on WorkspaceController (`workspace_stack.cpp`)
- ImageView thin routers; target gather covers Workspace selection + Image primary

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView (cross-mode content)
flip / rotate (pipeline bake + Image framing + Gallery pack)
duplicate / workspace clipboard (selection TU)

### Apply
```bash
git pull --ff-only …/biltoo-2496.1-own-workspace-placement-resets-7d823d8.bundle HEAD
```
