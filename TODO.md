# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2527.1-fix-ensureVisible-own-session-nav** (base `7d823d8`).

### Build fix
- `ensureVisibleItem`: include `imageitem.h` + `QGraphicsItem` cast (incomplete type after
  framing TU thin); use `ViewTransform::kEnsureVisibleMargin`

### Ownership transfer
- **ImageController::setImageModeNavigationEnabled / setGalleryReturnAvailable**
  — session-nav flags + hover clear + viewport update (ImageView thin routers)

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
- wheel zoom / pan shell
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2527.1-fix-ensureVisible-own-session-nav-7d823d8.bundle HEAD
```
