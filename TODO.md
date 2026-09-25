# TODO / agent handoff

## Status (2026-09-25)

**Tip:** `biltoo-2662.1-find-debug-env` (base `931af49`).
**Upstream:** origin/master at `931af49` (SessionSearchIndex + block_id Find already landed).

### This tip
- Gate informational `[find]` logs behind `BILTOO_DEBUG_FIND`.
- `ensurePageTextLayer: ok …` no longer spams every page during doc scan.
- Same gate for exportText / query / docScan summary lines in MainWindow.
- Failure paths (no client, empty URI, extract failed) still always `qWarning`.

### Prior (already on master)
- `SessionSearchIndex` — sparse Find tags by SessionImageId (+ path fallback).
- Filmstrip / gallery Find hit chrome; generation guard; unit tests.

### Apply
```bash
git pull --ff-only …/biltoo-2662.1-find-debug-env-931af49.bundle HEAD
# optional: BILTOO_DEBUG_FIND=1 biltoo …
```
