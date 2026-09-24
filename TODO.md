# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2556.1-own-scene-foreground-chrome** (base `7d823d8`).

### Ownership transfer
- **TextLayerController::paintSceneOverlays** — search hits, selection fill,
  region/link outlines (Image mode, scene space)
- **WorkspaceController::paintPageGuideOutline** — page outline + margin
  (Workspace, scene space)
- `drawForeground` dispatches controllers + gallery frames + shell viewport overlays

### Prior
**2555.1** Own viewport overlay paint on ViewShellChrome.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
drawForeground dispatch + bare-Gallery early-out + DPR transform setup

### Next thinning candidates
- setViewMode body (mode shell by design)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)
- drawForeground early-out / DPR setup → ViewShellChrome if dual needs it

### Apply
```bash
git pull --ff-only …/biltoo-2556.1-own-scene-foreground-chrome-7d823d8.bundle HEAD
```
