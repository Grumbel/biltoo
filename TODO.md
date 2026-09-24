# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2547.1-own-empty-session-invite-paint** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::paintEmptySessionInvite** — empty-canvas open/drop invite +
  edge-zone captions in viewport device pixels
- ImageView::paintViewportOverlays dispatches; drop private paintEmptySessionInvite

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / paintHudPanels / slideshow paint orchestration

### Next thinning candidates
- freezeItemAppearance residual
- paintHudPanels residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2547.1-own-empty-session-invite-paint-7d823d8.bundle HEAD
```
