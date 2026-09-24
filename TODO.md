# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2539.1-own-session-edit-peer-sync** (base `7d823d8`).

### Ownership transfer
- **DisplayPipelineController::syncSessionEditPeers** — peer display attach + flip
  for matching SessionImageId (live + Workspace/Gallery stashes)
- ImageView::commitItemSessionEdit calls the pipeline; drop private sync method

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance commit orchestration / paint / climb status / remaining status

### Next thinning candidates
- imageModeClimbActivityLabel (pipeline-coupled)
- copySessionAppearance / remaining appearance_commit
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2539.1-own-session-edit-peer-sync-7d823d8.bundle HEAD
```
