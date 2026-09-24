# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2545.1-own-workspace-viewport-chrome-paint** (base `7d823d8`).

### Ownership transfer
- **WorkspaceController::paintViewportChrome** — selection / page-guide chrome in
  viewport device pixels (skips when crop active or not Workspace)
- ImageView::paintViewportOverlays dispatches; drop private paintWorkspaceViewportChrome

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / paint orchestration / remaining status composition

### Next thinning candidates
- freezeItemAppearance residual
- gallery selection frame paint
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2545.1-own-workspace-viewport-chrome-paint-7d823d8.bundle HEAD
```
