# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.7-tile-lod-settle (base 2085c07).

### 2815.7
- tickPrimaryTileLod: check visible coverage *before* TileLoadCoordinator::tick
- Gallery decodeWatchdog: only tick tile LOD when on-screen cells still need tiles
- Stops post-settle 1 Hz coordinator wake + 16 ms timer re-arm when coverage is done

### Prior
- 2815.6 Performance panel + BackgroundWorkLog
- 2815.5 vips_concurrency_set(1)

### Apply
```bash
git pull --ff-only …/biltoo-2815.7-tile-lod-settle-2085c07.bundle HEAD
```

Fast-forward from ec5a7f87 (2815.6).
