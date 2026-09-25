# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2694.1-no-last-tile-stretch` (base `932ed5c`).

### 2694 — stop stretching right/bottom edge tiles
Last-column/row no longer extends dest past exclusive `step` to fill a
floor-half content remainder. ExactTile dest = exclusive src × 2^scale
(clamped to content), never stretched to fill the content rect.

### Apply
```bash
git pull --ff-only …/biltoo-2694.1-no-last-tile-stretch-932ed5c.bundle HEAD
```
