# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2411-masonry-fill-fit-avail** (base `d80d461`, includes 2403–2410).

### MasonryFill / MasonryRowsFill overshoot
Per-column (or per-row) scale to equalize the long axis **widened** short
columns past `availW` (rows past `availH`) — visible asymmetry / soft overshoot
without always tripping a scrollbar.

**Fix:** After equalization, `fitPackPosesToAvailWidth` / `…Height` applies a
global uniform scale about the pack origin so content exactly matches the
layout axis (bottoms / right edges stay aligned). Tests updated.

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2411.1-masonry-fill-fit-avail-d80d461.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag.

## Prior — 2410
Gallery pack measures with scrollbar gutters; PackViewportGuard in controller only.
