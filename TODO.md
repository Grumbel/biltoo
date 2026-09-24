# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2529.1-own-view-resized** (base `7d823d8`).

### Ownership transfer
- **ImageController::onViewResized** — Image-mode quality climb + sticky fit
- **GalleryController::onViewResized** — decode-window re-arm (no pack)
- **SlideshowController::onViewResizedDuringDwell** — atlas/zoom-blur invalidate
- ImageView::resizeEvent is mode dispatch only (Workspace still uses pipeline climb)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition / pan shell

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- pan shell (ViewportChrome; multi-mode)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2529.1-own-view-resized-7d823d8.bundle HEAD
```
