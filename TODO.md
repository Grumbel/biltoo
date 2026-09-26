# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.19-overlay-all-samples` (base `b9c3473`).

### 2713.19 — Overlay on every sample path (no cache policy change)
- paintSampleKindTag on pixmap/placeholder paths (not only drawSample)
- Tile plan wash unchanged; cell tags from 16 device px
- **Does not** disable ItemCoordinateCache for debug overlay

### Apply
```bash
git pull --ff-only …/biltoo-2713.19-overlay-all-samples-b9c3473.bundle HEAD
```
