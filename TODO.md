# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2491.1-own-perf-stats-in-hud** (base `7d823d8`).

### Ownership transfer
- **PerfStats** on HudChrome (`perf()`)
- TileNeighborPrefetch remains per-ImageView (dual-pane canvas membership)

### Residual on ImageView (intentional shell/host)
ViewFraming, ViewMode, ImageSizeCoordinator, TileNeighborPrefetch,
ImageModeSoftProvider, ItemWorld/path books, SessionShell, HudChrome,
ViewShellChrome, mode controllers

### Apply
```bash
git pull --ff-only …/biltoo-2491.1-own-perf-stats-in-hud-7d823d8.bundle HEAD
```
