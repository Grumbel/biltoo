# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2504.1-own-gallery-focus** (base `7d823d8`).

### Ownership transfer
- **focusItem / focusSessionId / focusSessionPath / revealPath / revealSessionId**
  on GalleryController (`gallerycontroller_focus.cpp`)
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `setWorkspacePaths` / `finishSetWorkspacePaths` (orchestration)
- geometry undo command helpers
- selection / transformTargets / primaryItem / targetItem / destroyCanvasItem

### Apply
```bash
git pull --ff-only …/biltoo-2504.1-own-gallery-focus-7d823d8.bundle HEAD
```
