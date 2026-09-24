# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2538.1-own-multiitem-status-pure** (base `7d823d8`).

### Ownership transfer
- **HudModel::galleryDebugPixelMixSuffix** — pure THUMTOO_DEBUG gallery mix line
- **HudModel::workspaceSelectedItemScaleSuffix** — pure Workspace selected scale/rot
- ImageView::statusTextMultiItem stays host (counts + pending decode + orchestration)

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance peer-sync / paint / climb status / remaining status

### Next thinning candidates
- syncSessionEditPeers residual
- imageModeClimbActivityLabel (pipeline-coupled)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2538.1-own-multiitem-status-pure-7d823d8.bundle HEAD
```
