# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.11-underlay-seed-on-size-tiles` (base `b9c3473`).

### 2713.11 — Seed underlay with size and after durable tiles
- `hasUsableUnderlaySample` → `ImageCache::hasUnderlay` (not main-slot soft)
- `sizeReady`: `scheduleStoreUnderlaySeed` when underlay slot empty
- `durableTilesReady`: re-seed (tiles often write Store LQIP) + tryInstall

### Apply
```bash
git pull --ff-only …/biltoo-2713.11-underlay-seed-on-size-tiles-b9c3473.bundle HEAD
```

## Prior
2713.10 underlay slot on origin; Kill Soft A–D

## Next
Runtime: pages without Store EMB/LQIP until tiles still lag; after pyramid expect more lqip=1

## Roadmap / later
### Kill Soft
[docs/KILL_SOFT.md](docs/KILL_SOFT.md)

### Tile draw / LOD investigation
[docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md)
