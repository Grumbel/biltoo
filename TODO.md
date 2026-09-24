# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2415-slideshow-tile-cover** (base `d80d461`).

### Slideshow tiles / up-res
- Shared `tilelod::prepare_and_paint_cover` (dest cover transform + tick + paint)
- Slideshow prefers live **ImageItem** TileLodController for the path (same climb as ImageView)
- Phase fallback still uses ensureFromTiles/ToTiles
- `min_scale = 0` (was durable floor → stuck coarse)
- Motion timer ticks primary TileLod so in-flight tiles complete
- Soft atlas only when tiles cannot paint yet

### Apply
```bash
git pull --ff-only …/biltoo-2415.1-slideshow-tile-cover-d80d461.bundle HEAD
```

Prior: 2414 pan-zoom factor ceiling; 2413 climb complete loop.
