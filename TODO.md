# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2596.1-export-paint-pipeline** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::paintHighResExportItems** — z-ordered high-res tile paint
- **DisplayPipelineController::contentExportBounds** — live content scene bounds
- ImageView routers; `renderExportImage` stays host (canvas background + pipeline paint)

### Prior
**2595.1** ImageController owns setItemSessionId + persistSessionAppearanceSlot  
**2594.1** ImageController owns flushAppliedContentToItemWorld  
**2593.1** SessionAppearance owns capture assemble + path persist

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / remember host residual,
renderExportImage (canvas bg + pipeline paint orchestration),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- freezeItemAppearance / rememberItemState (appearance host residual)
- takePendingSessionBindForNewItem
- slideshow coupling on setHudVisible

### Apply
```bash
git pull --ff-only …/biltoo-2596.1-export-paint-pipeline-7d823d8.bundle HEAD
```
