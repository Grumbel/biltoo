# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2525.1-own-color-grade** (base `7d823d8`).

### Ownership transfer
- **ImageController** owns interactive colour grade + deferred durable commit:
  `setTargetColorAdjustments`, `flushColorAdjustCommit`, `applyInteractiveColorGrade`
- ImageView keeps thin public routers (MainWindow / host surface)

### Prior in this stack (2522–2524)
- Transform reset signatures; attention session change
- Gallery emitItemFocus/Open; TextLayer tryMousePressLink; zoomIn/Out
- Workspace rotate uses PlacementLinear (drop undefined angleAt)

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
refreshScrollBarGeometry (view-matrix shell),
appearance load/apply / paint / input dispatch shell

### Next thinning candidates
- appearance apply/commit residual → ItemWorld / pipeline hosts
- paint / remaining input event TUs
- setViewMode body (mode shell by design)

### Apply
```bash
git pull --ff-only …/biltoo-2525.1-own-color-grade-7d823d8.bundle HEAD
```
