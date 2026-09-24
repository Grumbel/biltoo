# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2548.1-own-slideshow-overlay-paint** (base `7d823d8`).

### Ownership transfer
- **SlideshowController::paintLetterboxComposite** — zoom-blur letterbox underlay
- **SlideshowController::paintSeekbar** — timeline / dwell progress bar + clock
- ImageView::paintViewportOverlays dispatches; drop private slideshow paint methods

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / paintHudPanels / edge affordances

### Next thinning candidates
- freezeItemAppearance residual
- paintHudPanels residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2548.1-own-slideshow-overlay-paint-7d823d8.bundle HEAD
```
