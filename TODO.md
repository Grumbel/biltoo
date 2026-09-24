# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2592.1-fix-workspace-place-access** (base `7d823d8`).

### Fix
Compile fixes for workspace placement TUs moved in 2586/2587:
- `ImageView::LoadAdd` fully qualified for `scheduleImageLoad`
- `hostPersistGeometrySessionState` public host for pose-only persist
- Deferred drop lambda uses `ImageView *host` (not `host->m_view`)
- `defaultStateForPath` uses WorkspaceController's own method

### Prior
**2591.1** CropController owns crop appearance store/restore/apply  
**2590.1** ImageController copy appearance; SessionAppearance soft materialize  
**2589.1** SessionAppearance layout size; mode controllers own print

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- persistSessionAppearanceSlot / captureState (appearance host residual)
- setItemSessionId (identity host residual)
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2592.1-fix-workspace-place-access-7d823d8.bundle HEAD
```
