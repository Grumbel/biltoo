# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2514.1-own-gallery-enter** (base `7d823d8`).

### Ownership transfer
- **enterGallery** on GalleryController (`gallerycontroller_focus.cpp`)
- ImageView thin router

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration,
setViewMode / setActiveMode mode shell

### Still on ImageView
- geometry undo command helpers
- applyItemModeFlags / applyModeFlagsToLiveItems
- invalidateSessionLoads

### Apply
```bash
git pull --ff-only …/biltoo-2514.1-own-gallery-enter-7d823d8.bundle HEAD
```
