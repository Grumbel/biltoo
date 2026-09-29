# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.6-performance-panel (base 2085c07).

### 2815.6
- Panels -> Performance: Qt pool, thumtoo queue/activity, schedule deltas
- BackgroundWorkLog counters for probe/revalidate/pixels/tile/galleryDecode/tileLodTick
- Stacks on 2815.5 vips_concurrency_set(1)

### Prior
- 2815.5 vips concurrency 1 at ImageLoader::init
- 2815.3 formatLoadErrorMessage never exists

### Apply
```bash
git pull --ff-only …/biltoo-2815.6-performance-panel-2085c07.bundle HEAD
```

Fast-forward from origin tip bc3d1239 (2815.5).
