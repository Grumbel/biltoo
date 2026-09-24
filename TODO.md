# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2532.1-own-viewport-leave-mouseinfo** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::updateMouseInfo / onLeave** — hit-test mouse info + clear on leave
- **ImageController::onViewportLeave** — clear edge hover
- **GalleryController::onViewportLeave** — clear hover path
- **SlideshowController::onViewportLeave** — hide seekbar when not dragging
- ImageView::leaveEvent is dispatch; updateMouseInfo remains thin host API (Crop)

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition / drag-drop shell

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- drag-drop shell
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2532.1-own-viewport-leave-mouseinfo-7d823d8.bundle HEAD
```
