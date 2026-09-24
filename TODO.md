# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2528.1-own-wheel-zoom** (base `7d823d8`).

### Ownership transfer
- **ImageController::wheelZoomAboutCursor** — wheel zoom about cursor (Image / free-form
  Workspace after Gallery wheel tries). ImageView::wheelEvent is dispatch only.

### Prior in this stack (2522–2527)
- Transform reset; attention session; gallery open/focus; text link+hover; Ctrl+A
- zoomIn/Out; colour grade; angleAt; ensureVisible; session-nav enable

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition / pan shell

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- pan shell (ViewportChrome bag; multi-mode)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2528.1-own-wheel-zoom-7d823d8.bundle HEAD
```
