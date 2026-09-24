# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2549.1-own-hud-panels-paint** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::paintHudPanels** — pinned HUD panels, action flash, centre
  progress, session badge, filename (viewport device pixels)
- ImageView exposes hudFileName / loadingStatusHudLine as public status helpers
- paintViewportOverlays dispatches; drop private paintHudPanels

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
git pull --ff-only …/biltoo-2549.1-own-hud-panels-paint-7d823d8.bundle HEAD
```
