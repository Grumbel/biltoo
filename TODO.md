# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2537.1-own-content-appearance-reset-gallery** (base `7d823d8`).

### Ownership transfer
- **ItemWorld::hasContentEditComponents** — crop/bake/colour presence (not pose/attention)
- **GalleryController::onContentAppearanceReset** — pack + decode window after identity reset
- ImageView::targetHasContentAppearance / resetContentAppearanceForTargets use the above;
  Image fit path uses ImageController::fitItem

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance peer-sync / paint / remaining status

### Next thinning candidates
- syncSessionEditPeers residual
- remaining status composition (quality climb labels still view-coupled)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2537.1-own-content-appearance-reset-gallery-7d823d8.bundle HEAD
```
