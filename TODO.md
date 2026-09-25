# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2698.1-gallery-size-scale-race` (base `932ed5c`).

### 2698.1 — Gallery size probe vs pack scale race
SizeReady updated intrinsic layout size while pack placement scale lagged
(debounced ContentChange). contentRect grew under the old scale → cells looked
huge / not gallery-sized. Clearing galleryCellSize on aspect change made it
worse (boundingRect = full content until pack).

Fix:
- On size change for a packed cell, immediately rescale so footprint ≈ cell
  (contain); keep the cell clip.
- sizeReady always goes through applyProbedImageSize; no bare hostSetIntrinsicSize.

### 2697.1 — drop kTileOverlap symbol
### 2696 — exact tile math only

### Apply
```bash
git pull --ff-only …/biltoo-2698.1-gallery-size-scale-race-932ed5c.bundle HEAD
```
