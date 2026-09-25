# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2692.1-edge-tile-full-src` (base `932ed5c`).

### 2692 — edge tiles use full encoded bitmap as src
ExactTile src_uv is the full exclusive payload (not floor(content/factor)), so
right/bottom edge cells do not drop a source row/column when sizes disagree.

Pair with **thumtoo-340.5** (PDF level size = floor-half) and re-prepare tiles.

### Apply
```bash
git pull --ff-only …/biltoo-2692.1-edge-tile-full-src-932ed5c.bundle HEAD
```
