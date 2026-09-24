# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2544.1-own-capture-state-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::fillUnboundContentFromLiveAndPath** — unbound capture orient/crop
- **SessionAppearance::overlayAppliedContentXform** — mid-edit applied overlay
- **SessionAppearance::adoptPathSessionIndexHint** — path-map list-index hint
- ImageView::captureState stays host (ItemWorld + live grade/pose/session index)

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance / paint / remaining status composition

### Next thinning candidates
- freezeItemAppearance residual
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2544.1-own-capture-state-pure-7d823d8.bundle HEAD
```
