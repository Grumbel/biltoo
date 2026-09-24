# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2553.1-own-durable-freeze-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::preferDurableFreeze** — pure predicate (bound, no mid-edit
  applied ContentXform, durable row present)
- **SessionAppearance::durableFreezeFromParts** — assemble durable appearance +
  live pose/grade/path/sid/index
- `ImageView::freezeItemAppearance` keeps host orchestration (ItemWorld /
  live color / resolveContentEditSessionId); pure policy on SessionAppearance

### Prior
**2552.1** Fix ViewShellChrome paintHudPanels host access.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
freezeItemAppearance host residual (orchestration only),
paint orchestration (dispatch only)

### Next thinning candidates
- setViewMode body (mode shell by design)
- further freeze callers → host-only if dual surface needs it

### Apply
```bash
git pull --ff-only …/biltoo-2553.1-own-durable-freeze-pure-7d823d8.bundle HEAD
```
