# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2659.1-cross-region-text-search` (base `9740316`).

### Done
- Cross-region exact Find on reading-order stream (`TextSearchPolicy::findHits`).
- Approximate LTR partial-box highlight (startFrac/endFrac).
- Fuzzy still per-region (full box).
- Doc-wide page scan uses `findHits`.
- Unit tests: `textsearchpolicy` (6 passed in agent Qt 6.4 smoke).

### Host
```bash
ctest -R textsearchpolicy --output-on-failure
# UI: Find a phrase split across two PDF/EPUB boxes; highlight should be sub-rect
```

### Apply
```bash
git pull --ff-only …/biltoo-2659.1-cross-region-text-search-9740316.bundle HEAD
```
