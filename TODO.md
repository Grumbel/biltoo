# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2682.1-view-smooth-scaling` (base `932ed5c`).

### 2682 — View → Smooth Scaling
Checkable View menu action (default on). Process-wide via
`DisplayQuality::smoothScaling()`. When off: nearest-neighbour for tiles
(`tilePaintNeedsSmooth`), soft drawImage paths, ImageItem transformation mode,
and QGraphicsView hint. Persisted as `view/smoothScaling`.

### Prior
- 2681 exclusive dest + full overlap source (no expand/overdraw)
- 2679 last-tile coverage
- 2677 smooth density heuristic (still applied when Smooth Scaling is on)

### Apply
```bash
git pull --ff-only …/biltoo-2682.1-view-smooth-scaling-932ed5c.bundle HEAD
```
