# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2546.1-own-gallery-selection-frame-paint** (base `7d823d8`).

### Ownership transfer
- **GalleryController::paintSelectionFrames** — Gallery selection rings in scene
  space (drawForeground); skips interactive Workspace items
- ImageView drawForeground dispatches; drop private paintGallerySelectionFrames

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / paint orchestration (HUD, slideshow, empty invite)

### Next thinning candidates
- freezeItemAppearance residual
- paintEmptySessionInvite / paintHudPanels residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2546.1-own-gallery-selection-frame-paint-7d823d8.bundle HEAD
```
