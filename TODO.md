# TODO / agent handoff

## Status (2026-09-24)

**Tip: biltoo-2617.1-peel-itempaths-sessionids** (base `7d823d8`).

### This tip
Peel `itemPaths` / `itemSessionIds` — single MainWindow call site now walks
`liveItems()`. Header ~518 lines.

### Remaining shell queries on ImageView
- `selectedPaths` (mode-aware; keep as shell API)
- `pendingDecodeCount` (multi-controller gather)
- `imageSize` / `currentPath` (target/primary)
- `statusText` / `hudFileName` / …

### Apply
```bash
git pull --ff-only …/biltoo-2617.1-peel-itempaths-sessionids-7d823d8.bundle HEAD
```
