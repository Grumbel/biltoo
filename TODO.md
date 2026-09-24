# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2571.1-own-live-color-bind-native-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::preferLiveColor** — lag vs item grade for paint/status
- **SessionAppearance::BindLagAction** / **bindLiveColorLagAction** — bind lag
  stamp policy (no identity row pollution)
- **SessionAppearance::isPlaceholderProbeSize** / **isUsableNativeSize**
- Host keeps ItemWorld lag table and intrinsic size writes

### Prior
**2570.1** Own mode-leave sticky pan, HUD modified suffix, session index pure.

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
git pull --ff-only …/biltoo-2571.1-own-live-color-bind-native-pure-7d823d8.bundle HEAD
```
