# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2493.1-own-view-framing** (base `7d823d8`).

### Ownership transfer
- **ViewFraming** on ImageController (per-view, dual-safe)
- hostFraming() → m_image.framing()

### Residual on ImageView (intentional)
ViewMode, ViewShellChrome, HudChrome, SessionShell, TileNeighborPrefetch,
ImageSizeCoordinator, ImageModeSoftProvider, ItemWorld/path books,
QUndoStack, display pipeline pointer

### Apply
```bash
git pull --ff-only …/biltoo-2493.1-own-view-framing-7d823d8.bundle HEAD
```
