# TODO / agent handoff

## Status (2026-09-30)

**Tip:** biltoo-2872.1-ensure-pdf-sizes-on-miss (on `ea477d6` + agent stack).

### 2872.1
- On size miss for PDF/MD/text page refs: `ensure_pdf_page_sizes` then
  `get_size` before falling back to full `request_size`.
- Stops the size-gate tail (2300 fast → last ~100 forever) when some pages
  lack region dims in the Store.
- Pair with **thumtoo-015.1-ensure-pdf-page-sizes**.

### Prior
- 2871.1 parallel light size probe
- 2870.1 serial hydrate (regressed)

### Bundle policy
Work-line base: `ea477d6`. Full stack in each tip bundle.
