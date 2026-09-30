# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2876.1-tile-plan-overlay-text (on `ea477d6` + agent stack).

### 2876.1
- Tile plan overlay EXACT/PARENT (and summary) text: **constant on-screen size**
  (compensate painter scale); was proportional to cell → tiny at coarse s, huge
  at fine / negative s.
- Settled all-miss: `FAILED n/n` + `live denser` when s<0 + `no cells` when
  exact=0 (was opaque `ERROR n/n`).

### Prior
- 2875.1 tile scroll cancel
- 2874.1 TTFP baseline docs

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
