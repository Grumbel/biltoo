# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2518.1-own-image-fit-item** (base `7d823d8`).

### Ownership transfer
- **fitItem** on ImageController (`imagecontroller_framing.cpp`)
- ImageView thin router

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem host API (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin) / applyModeFlagsToLiveItems,
pushItemGeometryCommand / pushItemContentCommand (body in item/)

### Next thinning candidates
- zoomFit / zoomFill / zoomReset / zoomViewBy (framing_image residual)
- setViewMode body (mode shell by design)
- appearance / paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2518.1-own-image-fit-item-7d823d8.bundle HEAD
```
