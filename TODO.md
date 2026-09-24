# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2499.1-own-image-content-transform** (base `7d823d8`).

### Ownership transfer
- **flipHorizontal / flipVertical / rotateLeft / rotateRight /
  rotateContentByQuarterTurns** on ImageController (`imagecontroller_transform.cpp`)
- ImageView thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- `duplicateSelected` (Workspace + Gallery)
- setWorkspacePaths / rebind / destroy canvas orchestration
- geometry undo command helpers (`pushItemGeometryCommand`)

### Apply
```bash
git pull --ff-only …/biltoo-2499.1-own-image-content-transform-7d823d8.bundle HEAD
```
