# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2552.1-fix-hud-panels-host-access** (base `7d823d8`).

### Fix
- `ViewShellChrome::paintHudPanels` — use host APIs after 2549 move:
  - `m_view->hostPerf()` (was bare `m_hud.perf()`)
  - `m_view->targetItem()` / `primaryItem()`
  - `m_view->hostDisplayPipeline().pixelQualityLabel()` (private on ImageView)

### Prior
**2551.1** Own edge-affordance paint policy on ImageController.

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
git pull --ff-only …/biltoo-2552.1-fix-hud-panels-host-access-7d823d8.bundle HEAD
```
