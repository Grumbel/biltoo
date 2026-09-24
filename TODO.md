# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2565.1-own-soft-paint-state-pure** (base `7d823d8`).

### Ownership transfer
- **SessionAppearance::assembleSoftPaintState** — durable app + sparse Color →
  filmstrip/soft materialize state (false when identity)
- Gallery status pixel-mix uses **DisplayQuality::tierOf** (no local thresholds)
- `imageWithSessionAppearance` keeps ItemWorld / path-map / XDG host resolution

### Prior
**2564.1** Own status suffix formatters on HudModel.

### Residual on ImageView (intentional)
ViewMode, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline host APIs, fitItem/zoom host APIs (thin),
setViewMode / setActiveMode mode shell,
applyItemModeFlags (thin), pushItemGeometryCommand (body in item/),
freeze / flush / remember / persist host residual (orchestration only),
drawBackground / drawForeground one-line QGraphicsView overrides,
input event routers (QGraphicsView overrides)

### Next thinning candidates
- setViewMode body (mode shell by design — leave/enter orchestration)
- public thin routers (MainWindow API surface — keep until dual/callers migrate)

### Apply
```bash
git pull --ff-only …/biltoo-2565.1-own-soft-paint-state-pure-7d823d8.bundle HEAD
```
