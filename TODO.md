# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2569.1-own-crop-restore-gallery-enter** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::CropRestoreSource** / **cropRestoreSource** — priority for
  restoring crop (session store → sparse → live freeze → path map)
- **LayoutPrefs::galleryEnterMode** — FreeForm → Masonry on Gallery enter
- `loadRestoreCropAppearance` / `setViewMode` Gallery branch keep host loads

### Prior
**2568.1** Own content-edit detect pure on SessionAppearance.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist / crop-restore host residual,
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2569.1-own-crop-restore-gallery-enter-7d823d8.bundle HEAD
```
