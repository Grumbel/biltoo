# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2489.1-own-view-shell-chrome** (base `7d823d8`).

### Ownership transfer
- **ViewShellChrome** owns ViewportChrome + CanvasBackground
- hostChrome / hostCanvasBg forward to m_shell

### Residual on ImageView (intentional shell/host)
ViewFraming, ViewMode, SessionIdentity, SessionBindBook, PackOrderOverlay,
TileNeighborPrefetch, ImageModeSoftProvider, PerfStats, HudChrome, controllers

### Apply
```bash
git pull --ff-only …/biltoo-2489.1-own-view-shell-chrome-7d823d8.bundle HEAD
```
