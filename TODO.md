# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2536.1-own-content-appearance-propagate** (base `7d823d8`).

### Ownership transfer
- **GalleryController::onContentAppearancePropagated** — debounced pack on aspect/crop
- **ImageController::onContentAppearancePropagated** — Image underlay scene rect
- Workspace uses existing **updateSceneRect**
- ImageView::propagateSessionAppearanceToViews keeps filmstrip emits + mode dispatch

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance peer-sync / reset content / paint / remaining status

### Next thinning candidates
- syncSessionEditPeers residual
- resetContentAppearanceForTargets
- remaining status composition (quality climb labels still view-coupled)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2536.1-own-content-appearance-propagate-7d823d8.bundle HEAD
```
