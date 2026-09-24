# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2516.1-own-view-mode-flags** (base `7d823d8`).

### Ownership transfer
- **ViewModeFlags::applyToItem** pure policy (`view/viewmodeflags.cpp`)
- ImageView `applyItemModeFlags` thin host wrapper

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host orchestration,
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin) / applyModeFlagsToLiveItems

### Still on ImageView
- geometry undo command helpers (`pushItemGeometryCommand` + command class)

### Apply
```bash
git pull --ff-only …/biltoo-2516.1-own-view-mode-flags-7d823d8.bundle HEAD
```
