# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2530.1-own-viewport-pan** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome** owns multi-mode viewport pan (`tryMousePress/Move/ReleasePan`)
- ImageView binds `m_shell.bindView(this)` at construction; input dispatch calls `m_shell`

### Build note
- `ensureVisibleItem` fix is in this stack (2527+): needs `imageitem.h` + `QGraphicsItem` cast.
  If you still see `ensureVisible(item, 32, 32)` failures, pull this tip (or at least 2527).

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / status composition

### Next thinning candidates
- appearance apply/commit residual
- status text composition (HudModel already pure)
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2530.1-own-viewport-pan-7d823d8.bundle HEAD
```
