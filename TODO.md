# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.12-gallery-tile-density (base 2085c07).

### 2815.12 — scale 0 Gallery tiles
Root cause from Performance report: Gallery requested archive tiles at **s=0**
because `tileDevicePerContent()` used item `screenScale()` (~1 for cell-sized
items) while the tile grid is **native** resolution → target_scale=0 → full
JPEG decode per on-screen cell.

Fix: Gallery density = (cell scene long × view × dpr) / native long edge.

Also: PathRasterService no longer scheduleTilePyramid (FocusFull).

### Report signals that led here
- probe idle, focusFull=1, tile 148q/1r, running archive:… s=0 0,0

### Apply
```bash
git pull --ff-only …/biltoo-2815.12-gallery-tile-density-2085c07.bundle HEAD
```
