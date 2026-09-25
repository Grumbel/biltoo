# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2659.2-cross-region-text-search` (base `9740316`).

### Done
- Cross-region exact Find + LTR partial highlight (2659.1).
- **2659.2** Tight join only for near-touch same-line boxes (mid-word PDF
  splits); normal gaps still get a space so "hello world" works.
- Unit tests: **9 passed** (agent Qt 6.4 offscreen).

### Host
```bash
ctest -R textsearchpolicy --output-on-failure
```

### Apply
```bash
git pull --ff-only …/biltoo-2659.2-cross-region-text-search-9740316.bundle HEAD
```
