# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2531.1-own-double-click** (base `7d823d8`).

### Ownership transfer
- **ImageController::tryMouseDoubleClick** — edge nav / fullscreen
- **GalleryController::tryMouseDoubleClick** — open tile in Image mode
- **WorkspaceController::tryMouseDoubleClick** — handle drag or open Image mode
- ImageView::mouseDoubleClickEvent is pure dispatch

### Hygiene
- Dropped dead extra `return true` in GalleryController::tryMousePressGalleryLeft

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition / leave / drag-drop shell

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- leaveEvent / drag-drop shell
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2531.1-own-double-click-7d823d8.bundle HEAD
```
