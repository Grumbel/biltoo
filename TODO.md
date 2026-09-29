# TODO / agent handoff

## Status (2026-09-29)

**Tip:** biltoo-2815.13-gallery-scale-floor (base 2085c07).

### 2815.13
- tileDevicePerContent: always screen scene bounds / native (not only when cell size set)
- Gallery min_scale floored at density + hard floor when cell << native
- scheduleTilePyramid disabled unless BILTOO_FOCUSFULL=1 (prepareTiles still works)

Performance still showed s=0 + focusFull=1 after 2815.12 because non-GridCrop
layouts left m_galleryCellSize empty and FocusFull still ran.

