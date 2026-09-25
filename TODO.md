# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2683.1-exclusive-src-dest` (base `932ed5c`).

### 2683 — checkerboard / tile phase
Full 257→256 dest mapping **scaled every cell** and broke regular patterns
(visible vertical seam on grid.png). ExactTile now uses **exclusive source
and exclusive dest** (1:1 level pixels). Overlap strip is not drawn by QPainter.

### Also in stack
- 2682 View → Smooth Scaling
- 2681 removed dest expand/overdraw
- 2679 last-tile content coverage

### Apply
```bash
git pull --ff-only …/biltoo-2683.1-exclusive-src-dest-932ed5c.bundle HEAD
```
