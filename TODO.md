# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2554.1-drop-text-rubber-paint-forward** (base `7d823d8`).

### Ownership transfer
- `paintViewportOverlays` dispatches `m_textCtrl.paintRubberBandOverlay` directly
- Drop ImageView thin private `paintTextRubberBandOverlay` forward

### Prior
**2553.1** Own durable freeze pure policy on SessionAppearance.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
paint orchestration (dispatch only — no remaining private paint forwards)

### Next thinning candidates
- setViewMode body (mode shell by design)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2554.1-drop-text-rubber-paint-forward-7d823d8.bundle HEAD
```
