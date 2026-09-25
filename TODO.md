# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2695.1-tile-dest-grid-plus1` (base `932ed5c`).

### 2695 — tile dest = content grid + 1px seam overlap
ExactTile dest uses exclusive `tile_content_rect` (not bitmap×scale). Interior
cells add +1 content px on right/bottom (clamped) so a short encode cannot open
a 1px gap before the next tile. Outer edge is not stretched past content.

### Apply
```bash
git pull --ff-only …/biltoo-2695.1-tile-dest-grid-plus1-932ed5c.bundle HEAD
```
