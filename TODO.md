# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.1-tile-climb-wake (linear stack on `a889409`).

### 2892.1
- ImageView stuck on coarse tiles: `prepareTileLodPlan` early-return skipped
  `set_wake`; always install wake; re-arm while holding / in-flight.
- Slideshow stuck coarse on dwell: phase wake only updated the viewport —
  now `tickPrimaryTileLod` then update.

### 2891.x
- Open With… feature and fixes (see prior commits).

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
