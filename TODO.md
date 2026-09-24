# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2497.1-own-workspace-clipboard** (base `7d823d8`).

### Ownership transfer
- **captureSelectedClipboard / placeClipboardItems** on WorkspaceController
- ImageView thin routers for MainWindow paste/copy paths

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- flip / rotate (cross-mode pipeline + framing + Gallery pack)
- `duplicateSelected` (Workspace **and** Gallery — not Workspace-only)

### Apply
```bash
git pull --ff-only …/biltoo-2497.1-own-workspace-clipboard-7d823d8.bundle HEAD
```
