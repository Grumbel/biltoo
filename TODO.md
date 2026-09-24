# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2490.1-own-session-shell** (base `7d823d8`).

### Ownership transfer
- **SessionShell** owns SessionIdentity, SessionBindBook, PackOrderOverlay
- ImageView host accessors forward to m_session

### Residual on ImageView (intentional shell/host)
ViewFraming, ViewMode, ImageSizeCoordinator, TileNeighborPrefetch,
ImageModeSoftProvider, PerfStats, HudChrome, ViewShellChrome, controllers

### Apply
```bash
git pull --ff-only …/biltoo-2490.1-own-session-shell-7d823d8.bundle HEAD
```
