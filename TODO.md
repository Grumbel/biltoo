# TODO / agent handoff

## Status (2026-09-22)

**Tip: biltoo-2312-define-refresh-work-activity-status-bar.**

Define `MainWindow::refreshWorkActivityStatusBar()` (declared and called by
2311 / 5 Hz poll, but the body was omitted in the extract). Restores
archive / size / tile / soft status-bar painting. Link error fixed.

Requires **thumtoo-320** (batch warm activity finish fix).

### Apply
```bash
git -C thumtoo pull --ff-only …/thumtoo-320-activity-batch-warm-finish-8ea52ea.bundle HEAD
git -C biltoo pull --ff-only …/biltoo-2312-define-refresh-work-activity-status-bar-9d4418b.bundle HEAD
```

Next: **2313**.
