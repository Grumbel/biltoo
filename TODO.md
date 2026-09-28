# TODO / agent handoff

## Status (2026-09-28)

**Tip:** `biltoo-2756.1-slideshow-tile-wake` (base `9395b3e`).

### Done this tip
- Slideshow stuck on coarse tiles: pure-phase paint preferred hidden
  ImageItem LOD (no plan / no session wake). Prefer phase-owned tile
  sessions; always `set_wake` → viewport update when tiles complete.

### Prior
- 2755.1 / 2754.1: pkg-config systemd + leptonica
- 2753.1: filmstrip chrome dtor disconnect

### Apply
```bash
git pull --ff-only …/biltoo-2756.1-slideshow-tile-wake-9395b3e.bundle HEAD
```
