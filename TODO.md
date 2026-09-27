# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.4-gallery-return-viewport` (base `bcbb97e`).

### 2714.4 — Gallery return: no one-frame wrong camera
- `returnToGallery`: after `refreshScrollBarGeometry`, call `reassertViewport()`
  *synchronously*. `applyPendingRestore` was a no-op once enter cleared
  `m_pendingRestore`, so the view painted at origin until `singleShot(0)`.
- `prepareCanvas`: drop forced `viewport()->update()` (blank/scroll-0 frame
  before pack + restore).

### Prior (included)
2714.3 forget size memo · 2714.2 tile size clear · 2714.1 F5 selection

### Apply
```bash
git pull --ff-only …/biltoo-2714.4-gallery-return-viewport-bcbb97e.bundle HEAD
```
