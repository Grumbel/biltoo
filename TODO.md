# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2555.1-own-viewport-overlays-paint** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome::paintViewportOverlays** — full viewport-device-pixel overlay
  pass (text rubber, crop/attention, workspace chrome, slideshow letterbox/
  seekbar, edges, empty invite, HUD)
- `drawForeground` dispatches `m_shell.paintViewportOverlays`
- Drop ImageView private `paintViewportOverlays`
- `hostText()` accessor for TextLayerController (rubber-band paint)

### Prior
**2554.1** Drop text rubber-band paint thin forward.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
drawForeground scene-space gallery frames + transform setup (shell paint)

### Next thinning candidates
- setViewMode body (mode shell by design)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2555.1-own-viewport-overlays-paint-7d823d8.bundle HEAD
```
