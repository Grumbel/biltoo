# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2523.1-own-gallery-open-focus-text-link** (base `7d823d8`).

### Ownership transfer
- **GalleryController::emitItemFocus / emitItemOpenInImageMode** — id-safe session focus/open
  (was ImageView; only gallery callers + double-click shell routes through m_gallery)
- **TextLayerController::tryMousePressLink** — Image-mode page-link activation
- **ImageController::zoomIn / zoomOut** — view zoom steps

### Hygiene
- Dropped dead `sessionIdMatchesPath` (inlined into gallery emit helpers)
- framing_image / input link press: pure thin routers

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance / paint / colour-grade (next candidates)

### Next thinning candidates
- setViewMode body (mode shell by design)
- colour-grade / appearance commit bodies → ImageController
- paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2523.1-own-gallery-open-focus-text-link-7d823d8.bundle HEAD
```
