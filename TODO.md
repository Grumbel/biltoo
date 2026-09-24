# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2550.1-fix-slideshow-paint-const** (base `7d823d8`).

### Fix
- `SlideshowController::paintLetterboxComposite` / `paintSeekbar` definitions
  match the `const` declarations (compile error after 2549 ownership move).

### Prior
**2549.1** Own HUD panel paint on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / edge affordances / paint orchestration

### Next thinning candidates
- freezeItemAppearance residual
- drawEdgeAffordances residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2550.1-fix-slideshow-paint-const-7d823d8.bundle HEAD
```
