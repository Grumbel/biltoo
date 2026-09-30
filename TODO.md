# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2870.1-size-hydrate-before-probe (on `ea477d6` + agent stack).

### 2870.1
- `scheduleProbeBatch`: one worker hydrates process size memos via Store
  `get_size` (no LQIP/EMB), then `request_size` only for true misses.
- `warmSessionOpenMemos`: durable has_tile only — no parallel size walk that
  raced the probe FIFO (~300-hit bursts / ~1s gaps on large PDF sessions).
- Pair with **thumtoo-013.1-get-size-light**.

### Prior
- 2869.1 filmstrip grip size

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.

### Verify
```bash
BILTOO_TTFP=1 biltoo /path/to/many-page-pdfs
```
Warm Store: Resolving sizes should climb steadily without ~1s stalls; TTFP
should drop vs prior ~10s gate on ~2400 pages.
