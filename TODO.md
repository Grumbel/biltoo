# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2513.1-own-image-canvas-prep** (base `7d823d8`).

### Ownership transfer
- **prepareModeCanvas / clearSceneKeepingStashes** on ImageController
- ImageView thin routers (`prepareImageModeCanvas` / `clearSceneKeepingStashes`)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration

### Still on ImageView
- geometry undo command helpers
- setViewMode / setActiveMode / mode enter-leave shell
- applyItemModeFlags

### Apply
```bash
git pull --ff-only …/biltoo-2513.1-own-image-canvas-prep-7d823d8.bundle HEAD
```
