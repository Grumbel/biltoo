# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2677.1-smooth-only-1x-2x` (base `932ed5c`).

### Filtering vanished at 4700% (and other zooms)
`tilePaintNeedsSmooth` treated **any** near-integer tile→device density ≥1 as
nearest-neighbour. At high zoom (scale 0, dpp ≈ zoom%) that hit every integer
percent stop (4700%, 300%, …) and dropped bilinear filtering.

**2677:** skip Smooth only for true **1:1 / 2:1** density (ε=0.04); keep smooth
for 3× and above. Docs: `docs/TILE_LOD.md` paint-transform note.

### Prior tip (already on origin)
2676 seam overdraw — still relevant; JPEG independent encode residual remains.

### Apply
```bash
git pull --ff-only …/biltoo-2677.1-smooth-only-1x-2x-932ed5c.bundle HEAD
```
