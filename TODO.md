# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2492.1-residual-ownership-inventory** (base `7d823d8`).

### Ownership transfer series (2471–2491)
Mode bags and timer residuals moved onto Workspace / Gallery / Image /
Text / Slideshow / HudChrome / SessionShell / ViewShellChrome /
DisplayPipelineController.

### Residual on ImageView (intentional — see IMAGEVIEW_ITEM_OWNERSHIP §2492)
ViewFraming, ViewMode, ViewShellChrome, HudChrome, SessionShell,
TileNeighborPrefetch, ImageSizeCoordinator, ImageModeSoftProvider,
ItemWorld/path books, QUndoStack, display pipeline pointer

### Next (if continuing)
Only move a residual with an explicit dual-pane / host-interface plan.
Do not move TileNeighborPrefetch onto a shared pipeline without a
per-surface host bridge.

### Apply
```bash
git pull --ff-only …/biltoo-2492.1-residual-ownership-inventory-7d823d8.bundle HEAD
```
