# TODO / agent handoff

## Status (2026-09-27)

**Tip:** `biltoo-2714.13-scroll-min-tiles` (base `bcbb97e`).

### 2714.13 — Keep min-scale tiles on screen while scrolling
Virtual plan cells paint retained `TileLodRegistry` tiles via
`prepare_and_paint_cover` (durable min_scale) before EMB/LQIP fallback.
`touch()` keeps on-screen paths preferred under process LRU.

### Apply
```bash
git pull --ff-only …/biltoo-2714.13-scroll-min-tiles-bcbb97e.bundle HEAD
```
