# TODO / agent handoff

## Status (2026-09-22)

**Tip: biltoo-2310-work-status-poll.**

- Verified thumtoo `test_activity` (standalone) still ok against tip 319.
- Status bar work line polls at ~5 Hz while `workActivityBusy()` so counts move
  during long extracts without relying on sparse `statusChanged`.
- Poll uses `refreshWorkActivityStatusBar()` only (no metadata/nav refresh).
- `updateStatus` reentrancy guard (`m_inUpdateStatus`).

Requires **thumtoo-319**.

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-319-activity-tiles-soft-8ea52ea.bundle HEAD
git -C biltoo  pull --ff-only …/biltoo-2310-work-status-poll-c0bffd5.bundle HEAD
```

Next: **2311** (optional debug activity panel / TOC archive i/n).
