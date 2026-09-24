# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2535.1-own-workspace-saved-appearance** (base `7d823d8`).

### Ownership transfer
- **WorkspaceController::updateSavedAppearanceFromItem** — durable Workspace snapshot
  pose after session content edit (content stays on ItemWorld sparse tables)
- ImageView::commitItemSessionEdit calls the controller; drop private
  updateWorkspaceSavedAppearance

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance commit peer-sync / propagate / paint / remaining status

### Next thinning candidates
- syncSessionEditPeers / propagateSessionAppearanceToViews residual
- resetContentAppearanceForTargets
- remaining status composition (quality climb labels still view-coupled)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2535.1-own-workspace-saved-appearance-7d823d8.bundle HEAD
```
