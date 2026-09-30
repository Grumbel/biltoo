# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2842.1-slideshow-tile-min-scale-0 (on origin `1824408`).

### 2842.1
- Slideshow `paintSlideshowTiles`: `min_scale` **0** (shared `prepare_and_paint_cover`).
  Paint had `kDocumentLiveMinScale` while tick stayed at 0 → incomplete −1 grids
  (cropped tile subset). Image mode still uses `kDocumentLiveMinScale` for zoom.

### On origin already (1824408)
- Double View annotations multi-`liveItems` path
- Empty invite when sessionN > 0
- HudModel tr plurals (40b320f)

### Required thumtoo
thumtoo-010.3-document-page-count-store (on origin `b37b5e2`) preferred
