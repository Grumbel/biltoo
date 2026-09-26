# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.10-underlay-slot-on-origin` (base `b9c3473`; linear on origin).

### 2713.10 — Rebase onto origin + ImageCache underlay slot
Stacks cleanly on origin `b2eeb7e` (2713.7 install fix already pushed).
- `-Wshadow`: inner `live` → `embLive`
- ImageCache **underlay slot** (`getUnderlay`/`hasUnderlay`) so soft cannot block LQIP
- Size probe warm hit requires `hasUnderlay`
- tryInstall / setHasLqip / cachedLqipImage use underlay slot

### Apply
```bash
git pull --ff-only …/biltoo-2713.10-underlay-slot-on-origin-b9c3473.bundle HEAD
```

## Next
Runtime: consistent `lqip=1` when Store has EMB/LQIP with size

## Roadmap / later
### Kill Soft
[docs/KILL_SOFT.md](docs/KILL_SOFT.md) — A–D done

### Tile draw / LOD investigation
[docs/TILE_DRAW_INVESTIGATION.md](docs/TILE_DRAW_INVESTIGATION.md)
