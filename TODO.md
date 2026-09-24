# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2551.1-own-edge-affordance-paint-policy** (base `7d823d8`).

### Ownership transfer
- **ImageController::drawEdgeAffordances** — sole edge-chevron paint + suppress
  policy (crop/attention, mode, nav flags). Matches `edgeZoneAt`.
- `paintViewportOverlays` dispatches `m_image.drawEdgeAffordances` directly.
- Drop ImageView thin `drawEdgeAffordances` private forward.

### Prior
**2550.1** Fix const on slideshow letterbox/seekbar paint.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance residual, paint orchestration (dispatch only)

### Next thinning candidates
- freezeItemAppearance residual (host freeze; pure merges already on SessionAppearance)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2551.1-own-edge-affordance-paint-policy-7d823d8.bundle HEAD
```
