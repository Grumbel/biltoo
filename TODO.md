# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.15-overlay-emb-lqip-tile-ram` (base `b9c3473`).

### 2713.15 — Tile debug overlay EMB/LQIP + larger tile RAM
**Overlay** (debug wash):
- Yellow = EXACT, orange = PARENT
- Magenta = **EMB**, cyan = **LQIP**, blue = HOLE
- Cell tags when ≥40 device px; summary plate ends with EMB/LQIP when underlay present

**Tile RAM** (scroll was re-fetching edge cells):
- Per-path budget default **512 MiB** (was 128)
- Global registry **768 MiB** / max idle paths **128** (was 384 / 64)
- Protect **1-cell ring** around visible keys so small pans keep tiles
- Override: `BILTOO_TILE_RAM_MIB`, `BILTOO_TILE_MAX_IDLE`

### Apply
```bash
git pull --ff-only …/biltoo-2713.15-overlay-emb-lqip-tile-ram-b9c3473.bundle HEAD
```

## Prior
2713.14 ERROR settled; underlay slot; Kill Soft; thumtoo-344.4 region page size
