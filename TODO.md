# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2522.1-fix-transform-reset-thin** (base `7d823d8`).

### Build fix
- `resetItemScale` / `resetItemRotation` / `resetItemShear` match no-arg header + WorkspaceController
- Dropped dead `ImageView::snapRotationDegrees` (callers use `PlacementLinear` / crop snap)

### Ownership / hygiene
- **AttentionController::onCurrentSessionChanged** owns attention draft reload on session id change
- ImageView selection / canvas_focus / session_remove TUs: pure thin routers (dead includes gone)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell)

### Next thinning candidates
- setViewMode body (mode shell by design)
- appearance / paint / input event TUs

### Apply
```bash
git pull --ff-only …/biltoo-2522.1-fix-transform-reset-thin-7d823d8.bundle HEAD
```
