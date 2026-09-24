# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2584.1-export-display-pipeline** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::blockingExportDisplayForItem** — blocking load +
  materialize off GUI (export/print high-res path)
- ImageView::blockingExportDisplayForItem is a thin router
- Slimmed imageview_export.cpp includes (~201 → ~137 lines)

### Prior
**2583.1** Fix hostCentreProgress dup; pad colour + edit marks on chrome  
**2582.1** Mode controllers own pending-decode path counts  
**2581.1** Viewport drag events on shell; transform undo with geometry

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- public thin routers beyond materials (keep until dual/callers migrate)
- slideshow coupling on setHudVisible (SlideshowController host)
- paintHighResExportItems scene walk (stays host until dual/callers allow)

### Apply
```bash
git pull --ff-only …/biltoo-2584.1-export-display-pipeline-7d823d8.bundle HEAD
```
