# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2686.1-no-assemble` (base `932ed5c`).

### 2686 — roll back assemble-then-smooth
Per-frame full-buffer composite starved the GUI. Back to **per-tile** exclusive
src→dest `drawImage`. Seams with Smooth Scaling may remain; performance first.

### Keep
- Exclusive src_uv (no 257→256 cell scale)
- View → Smooth Scaling
- Smooth only forced off near 1×/2× tile density
- thumtoo kTileOverlap=0 (separate bundle)

### Apply
```bash
git pull --ff-only …/biltoo-2686.1-no-assemble-932ed5c.bundle HEAD
```
