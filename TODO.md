# TODO / agent handoff

## Status (2026-09-23)

**Tip: biltoo-2409-tile-tick-gui-budget** (base `d80d461`, includes 2403–2408).

### TileLoadCoordinator multi-second GUI budgets
After durable tiles exist (e.g. `thumtoo-prepare --tiles` on a PDF),
`TileLoadCoordinator::tick` logged 500–2500 ms. Causes:

1. thumtoo `request_tile(s)` did `get_tile` on the **caller** (GUI) — fixed in
   **thumtoo-336** (always queue workers).
2. biltoo decoded JPEG→rgba8 on the Qt executor (GUI) in `requestTiles`.
3. Coordinator allowed too many targets/shares and pumped unlimited completions.

**This tip (biltoo):**
- `requestTiles`: JPEG decode on `QThreadPool`, rgba8 delivered via queued
  invoke (rgb888/rgba8 stay inline — cheap).
- Coordinator: wall 8/12 ms, max 6/1 targets, smaller per-cell shares.
- `TileSession::pump`: at most 16 completions per call (rest next tick).

**Pair with thumtoo ≥ 336.**

### Apply
```bash
git -C biltoo pull --ff-only …/biltoo-2409.1-tile-tick-gui-budget-d80d461.bundle HEAD
git -C thumtoo pull --ff-only …/thumtoo-336.1-tile-request-no-gui-sqlite-f71d183.bundle HEAD
```

**Next:** RC smoke; VERSION 0.2.0 + tag (after verifying PDF gallery scroll).

## Backlog (0.2.0)
- [x] 2381–2408
- [x] 2409 tile tick GUI budget
- [ ] RC smoke
- [ ] VERSION 0.2.0 + tag
