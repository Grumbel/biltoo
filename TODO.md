# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2681.1-exclusive-dest-full-src` (base `932ed5c`).

### 2681 — correct QPainter overlap model
Dest expand / seam overdraw were wrong for QPainter.

- **Dest** = exclusive W×H (`tile_content_rect` only).
- **Source** = full bitmap including +1 overlap when present.
- `drawImage(exclusive, full_src)` so SmoothPixmapTransform filters toward the
  shared edge line; cells abut, no dest overlap.

Removed: ExactTile dest expand, `±gap` / `+gap` overdraw, R→L order requirement.

### Still in stack
- 2679 last-tile content coverage (odd-width floor-half)
- 2677 smooth only at 1×/2×
- Research doc updated §15

### Residual
- JPEG dual-encode of the shared strip (colour step, not a gap).
- Legacy 256× Store tiles: no overlap source; still exclusive paint.

### Apply
```bash
git pull --ff-only …/biltoo-2681.1-exclusive-dest-full-src-932ed5c.bundle HEAD
```
