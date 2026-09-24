# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2635.1-gallery-align-center** (base `2c570d4`).

### This tip
Revert Gallery QGraphicsView alignment to AlignCenter (was AlignLeft|AlignTop
in 2630 to mask phantom scrollbar gutters). Zoom-out was pinning the pack to
the top-left corner. Centring is correct; return-from-Image scroll is handled
by 2634 scene-centre restore.

### Apply
```bash
git pull --ff-only …/biltoo-2635.1-gallery-align-center-2c570d4.bundle HEAD
```
