# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2410-gallery-pack-scrollbar-gutter** (base `d80d461`, includes 2403–2409).

### Gallery dual-scrollbar feedback loop
Some layouts packed to the full viewport (AsNeeded, no bars). One bar then
shrank the client and the other axis overshot → dual bars.

**Fix:**
- `GalleryPackFit::PackViewportGuard` — AlwaysOn both axes, read viewport, restore.
- Used by **applyLayout** and **rebuildVirtualPlan** (virtual path was measuring
  without gutters).
- `packAvailAxis` subtracts `kPackAxisSlackPx` (1px).
- `clampSceneRectToPack` on the fitted axis after scene bounds.
- Overshoot epsilon `0.5` px (was `1e-4`).

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2410.1-gallery-pack-scrollbar-gutter-d80d461.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag.

## Backlog (0.2.0)
- [x] 2381–2409
- [x] 2410 gallery pack scrollbar gutter
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
