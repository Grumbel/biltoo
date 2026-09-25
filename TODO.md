# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2676.1-tile-seam-overdraw` (base `2e49220`).

### Seams after cache clear
Overlap (257) helps bilinear continuity but **JPEG encodes each cell
independently** — the shared column still diverges after decode (worst when
coarse tiles are upscaled). QPainter can also leave **subpixel hairlines**.

2676: sort paint L→R/T→B; overdraw ~0.75 device-px in content space on both
paint_draw_plan and ImageItem paths. ExactTile still expands for +1 overlap.

### Apply
```bash
git pull --ff-only …/biltoo-2676.1-tile-seam-overdraw-2e49220.bundle HEAD
```
