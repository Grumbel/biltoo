# TODO / agent handoff

## Status (2026-10-05)

**Tip:** biltoo-2892.11-issue-diag (linear stack on `a889409`).

### 2892.11
- Diagnostics only: IssueDiag on last issue_requests (early/cand/bat/skip*).
- Appended to BILTOO_TILE_DEBUG path line after `|`.

### How to read
- early=1 no source, 2 budget<=0, 3 content_w<=0
- cand= scored for issue after gates; bat= handed to TileSource
- skipFail= Failed this gen (no-spam); skipDen= denser blocked without s=0
- miss>0 + bat=0 + cand=0 → all gated; cand>0 bat=0 → budget/race
- bat>0 + inflight=0 later → completions dropped or never delivered

### Bundle policy
Work-line base: `a889409`. Full stack `a889409..HEAD`.
