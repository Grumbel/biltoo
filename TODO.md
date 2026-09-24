# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2543.1-own-appearance-copy-identity-pose** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::appearanceCopyWithIdentityPose** — pure content copy with
  identity free-placement pose for drop-duplicate / filmstrip fork
- ImageView::copySessionAppearance uses the pure helper; keeps donor resolve +
  filmstrip emits

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freeze/capture state / paint / remaining status composition

### Next thinning candidates
- freezeItemAppearance / captureState residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2543.1-own-appearance-copy-identity-pose-7d823d8.bundle HEAD
```
