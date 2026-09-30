# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2873.1-size-probe-cleanup (on `ea477d6` + agent stack).

### 2873.1
- Drop host-side `ensure_pdf_page_sizes` special case — thumtoo `request_size`
  ensures multipage dims on miss (016.1).
- Comments: size probe vs durable warm ownership clarified.

### Prior
- 2872.1 ensure on miss (host)
- 2871.1 parallel light probe

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.

### Perf reference
Gallery multipage PDF TTFP baseline (~2400 pages, settled Store): **docs/TTFP.md § Baseline 2026-09-30** (biltoo `3907788` + thumtoo `d6a341f`, ~0.5 s first pixels).
