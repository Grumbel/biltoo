# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2693.1-exclusive-src-cap-assert` (base `932ed5c`).

### 2693
- Cap ExactTile src at kTileSize (no 257→256 scale from leftover Store cells)
- Soft-fail tileLodBag without bag (no assert abort)
- ~0.5 device-px right/bottom dest overdraw to close float gaps

Missing lines every 256px with leftover 257 tiles = 257→256 squash. Purge/
re-prepare tile cache. With Smooth on, residual filter seams can remain.

### Apply
```bash
git pull --ff-only …/biltoo-2693.1-exclusive-src-cap-assert-932ed5c.bundle HEAD
```
