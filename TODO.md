# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2871.1-parallel-light-size-probe (on `ea477d6` + agent stack).

### 2871.1
- Revert serial single-thread Store hydrate (was ~10× slower).
- `scheduleProbeBatch` again uses bounded parallel FIFO (`kMaxConcurrentSizeProbes=16`).
- `requestSizeAsync`: try light `get_size` first; `request_size` only on miss.
- Keep no dual size walk in `warmSessionOpenMemos` (durable only).
- Pair with **thumtoo-014.1-region-size-load** (critical: region dims actually load).

### Prior
- 2870.1 serial hydrate (regressed — superseded)
- 2869.1 filmstrip grip size

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.

### Verify
```bash
BILTOO_TTFP=1 biltoo /path/to/many-page-pdfs
```
Warm Store: sizes should resolve quickly without re-opening PDFs.
