# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2631.1-prepare-tile-cache-dialog** (base `560319b`).

### This tip — Prepare Tile Cache dialog
- Edit → **Prepare Tile Cache…**
- Dialog: session cache stats (have / missing / unsupported), detail level
  (full→overview → min_scale 0..3), progress bar, cancel
- `ThumtooCache::queryTilePrepareStats` + `prepareTiles` (worker; waits on
  pyramid completion; LQIP opportunistic via thumtoo tile encode)
- Tiles only — no ladder / sizes-only UI

### Apply
```bash
git pull --ff-only …/biltoo-2631.1-prepare-tile-cache-dialog-560319b.bundle HEAD
```
