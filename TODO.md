# TODO / agent handoff

## Status (2026-09-22)

**Tip: biltoo-2311-status-dry-refresh.**

updateStatus uses refreshWorkActivityStatusBar() (same as 5 Hz poll).
Requires **thumtoo-320** (batch warm activity finish fix).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-320-activity-batch-warm-finish-8ea52ea.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2311-status-dry-refresh-c0bffd5.bundle HEAD
```

Next: **2312**.
