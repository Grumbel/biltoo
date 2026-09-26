# TODO / agent handoff

## Status (2026-09-26)

**Tip:** `biltoo-2713.16-overlay-path-arg` (base `b9c3473`).

### 2713.16 — Fix paintTilePlanDebugOverlay path
Free-function overlay takes `itemPath` for EMB/LQIP underlay classification
(cannot call ImageItem::path()).

### 2713.15 — Overlay EMB/LQIP + larger tile RAM
Magenta EMB / cyan LQIP / blue HOLE; 512 MiB path / 768 MiB global; protect ring.
`BILTOO_TILE_RAM_MIB`, `BILTOO_TILE_MAX_IDLE`.

### Apply
```bash
git pull --ff-only …/biltoo-2713.16-overlay-path-arg-b9c3473.bundle HEAD
```
