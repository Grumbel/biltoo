# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2691.1-no-tile-overlap` (base `932ed5c`).

### 2691 — remove 257 overlap
`kTileOverlap = 0`. No parent-UV strip. Exclusive paint only.

Pair with **thumtoo-340.4-no-tile-overlap**. Re-prepare tile caches.

### Apply
```bash
git pull --ff-only …/biltoo-2691.1-no-tile-overlap-932ed5c.bundle HEAD
```
