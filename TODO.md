# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2585.1-probed-size-pipeline** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::applyProbedImageSize** — live-tile layout size from
  probe, gallery cell fixup, Image framing, pack, slideshow atlas
- ImageView::applyProbedImageSize is a thin router
- Slimmed `imageview_modes.cpp` includes (only mode shell needs)

### Prior
**2584.1** Pipeline owns blocking export display materialize  
**2583.1** Fix hostCentreProgress dup; pad colour + edit marks on chrome  
**2582.1** Mode controllers own pending-decode path counts

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell (leave/enter orchestration),
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
contentLayoutSize / placeOrMoveImageAt / addImageForSession host residual,
paintHighResExportItems / renderExportImage (scene walk + canvas bg),
drawBackground / drawForeground / paintEvent / input one-line QGraphicsView overrides,
public thin routers (MainWindow API surface),
status host orchestration; setHudVisible still syncs slideshow timer

### Next thinning candidates
- setViewMode body remains mode shell
- public thin routers beyond materials (keep until dual/callers migrate)
- slideshow coupling on setHudVisible (SlideshowController host)
- placeOrMoveImageAt / addImageForSession (Workspace/session host residual)

### Apply
```bash
git pull --ff-only …/biltoo-2585.1-probed-size-pipeline-7d823d8.bundle HEAD
```
